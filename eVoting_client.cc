#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <vector>
#include <time.h>
#include <sodium.h>
#include <grpcpp/grpcpp.h>
#include "eVoting.grpc.pb.h"

using google::protobuf::Timestamp;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using voting::AuthRequest;
using voting::AuthToken;
using voting::Challenge;
using voting::Election;
using voting::ElectionName;
using voting::ElectionResult;
using voting::eVoting;
using voting::Response;
// using voting::Status;
using voting::Vote;
using voting::VoteCount;
using voting::Voter;
using voting::VoterName;

std::string server_address;
std::string backup_address;

std::time_t strToTime(std::string input_time)
{
  std::tm time_struct = {};
  std::stringstream ss(input_time);
  ss >> std::get_time(&time_struct, "%Y-%m-%dT%H:%M:%SZ");
  std::time_t time = std::mktime(&time_struct);
  return time;
}

class VotingClient
{
public:
  VotingClient(std::shared_ptr<Channel> channel)
      : stub_(eVoting::NewStub(channel)) { isbackup = 0; }

  void connectBackup()
  {
    isbackup = 1;
    backupChannel = grpc::CreateChannel(backup_address, grpc::InsecureChannelCredentials());
    backupStub_ = eVoting::NewStub(backupChannel);
    // std::cout << "building connection" << std::endl;
  }

  std::string preAuth()
  {
    std::cout << "please input your name:";
    std::cin >> voter_name;
    ClientContext context;
    VoterName name;
    Challenge test;

    name.set_name(voter_name);

    // RPC
    Status status;

    status = stub_->PreAuth(&context, name, &test);
    if (status.error_code() == 14)
    {
      // std::cout << "failed to connect primary server, tring to connect backup server..." << std::endl;
      if (!isbackup)
        connectBackup();
      ClientContext newContext;
      status = backupStub_->PreAuth(&newContext, name, &test);
    }

    if (status.ok())
    {
      return test.value();
    }
    else if (status.error_code() == 1)
    {
      std::cout << "login fail, no this user name" << std::endl;
    }
    // else if (status.error_code() == 14)
    // {
    //   std::cout << "failed to connect backup server" << std::endl;
    // }

    return "RPC failed";
  }

  // rpc Auth (AuthRequest) returns (AuthToken)
  int Auth(std::string challenge)
  {
    std::string fileName;
    char sktmp[crypto_sign_SECRETKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    std::cout << "please input your secret key file:";
    std::cin >> fileName;

    // open secret key file
    std::fstream file;
    file.open(fileName, std::fstream::in | std::fstream::binary);
    while (file.is_open() == 0)
    {
      std::cout << fileName << " open file fail, please input your secret key file again:";
      std::cin >> fileName;
      file.open(fileName, std::fstream::in | std::fstream::binary);
    }
    file.read((char *)sk, crypto_sign_SECRETKEYBYTES);
    file.close();

    // detached signature
    // unsigned char sk[crypto_sign_SECRETKEYBYTES];
    // for (int i = 0; i < crypto_sign_SECRETKEYBYTES; i++)
    //   sk[i] = (unsigned char)sktmp[i];
    unsigned char sig[crypto_sign_BYTES];
    unsigned char *massage = (unsigned char *)challenge.c_str();
    crypto_sign_detached(sig, NULL, massage, challenge.length(), sk);

    // AuthRequest.response is the detached signature of Challenge.
    // set grpc parameter
    Response *res = new Response();
    VoterName *name = new VoterName();
    AuthRequest authrequest;
    ClientContext context;
    AuthToken authtoken;

    name->set_name(voter_name);
    res->set_value((char *)sig);
    authrequest.set_allocated_response(res);
    authrequest.set_allocated_name(name);
    // RPC
    Status status = stub_->Auth(&context, authrequest, &authtoken);
    if (status.error_code() == 14)
    {
      if (!isbackup)
        connectBackup();
      ClientContext newContext;
      status = backupStub_->Auth(&newContext, authrequest, &authtoken);
    }
    if (!status.ok())
    {
      if (status.error_code() == 1)
      {
        std::cout << "Auth deny by server, failed" << std::endl;
      }
      // else if (status.error_code() == 14)
      // {
      //   std::cout << "failed to connect backup server" << std::endl;
      // }
      return 1;
    }
    else if (authtoken.has_value())
    {
      token = authtoken;
      std::cout << "Auth successed" << std::endl;
      return 0;
    }
    return 1;
  }

  // rpc CreateElection (Election) returns (Status)
  void CreateElection()
  {
    std::cin.ignore();
    std::vector<std::string> grouplist, choicelist;
    std::string _electionName, _choiceName, _groups, _end_date;
    std::cout << "Input election name:";
    std::getline(std::cin, _electionName);
    std::cout << "Input groups name(format: group1 group2 ... ):";
    std::getline(std::cin, _groups);
    std::cout << "Input choice name(format: choice1 choice2 ... ):";
    std::getline(std::cin, _choiceName);
    std::cout << "Input end date (format: 2023-01-01T00:00:00Z ):";
    std::cin >> _end_date;

    // split _groups, _choiceName
    std::stringstream s1(_groups);
    std::stringstream s2(_choiceName);
    std::string tmp;
    while (true)
    {
      s1 >> tmp;
      if (s1.fail())
        break;
      grouplist.push_back(tmp);
    }
    while (true)
    {
      s2 >> tmp;
      if (s2.fail())
        break;
      choicelist.push_back(tmp);
    }
    time_t endtime = strToTime(_end_date);

    // grpc parameter
    Election *election = new Election();
    ClientContext context;
    voting::Status vstatus;
    for (int i = 0; i < grouplist.size(); i++)
    {
      election->add_groups(grouplist[i]);
    }
    for (int i = 0; i < choicelist.size(); i++)
    {
      election->add_choices(choicelist[i]);
    }

    election->set_name(_electionName);
    voting::AuthToken *tok = new voting::AuthToken();
    tok->set_value(token.value());
    election->set_allocated_token(tok);

    Timestamp *end_time = new Timestamp();
    end_time->set_seconds(endtime);
    end_time->set_nanos(0);
    election->set_allocated_end_date(end_time);

    // test input data
    std::cout << "election:" << _electionName << std::endl;
    std::cout << "groups:" << _groups << std::endl;
    std::cout << "choiceName:" << _choiceName << std::endl;
    std::cout << "end date:" << _end_date << std::endl;

    // RPC
    Status status = stub_->CreateElection(&context, *election, &vstatus);
    if (status.error_code() == 14)
    {
      if (!isbackup)
        connectBackup();
      ClientContext newContext;
      status = backupStub_->CreateElection(&newContext, *election, &vstatus);
    }

    if (!status.ok())
    {
      std::cout << "CreateElection RPC failed" << std::endl;
    }
    else if (vstatus.code() == 0)
    {
      std::cout << "CreateElection successed" << std::endl;
    }
    else if (vstatus.code() == 1)
    {
      std::cout << "Invalid authentication token" << std::endl;
    }
    else if (vstatus.code() == 2)
    {
      std::cout << "Missing groups or choices specification" << std::endl;
    }
    else if (vstatus.code() == 3)
    {
      std::cout << "Unknown error" << std::endl;
    }
    return;
  }

  // rpc CastVote (Vote) returns (Status)
  void CastVote()
  {
    ClientContext context;
    Vote vote;
    voting::Status vstatus;
    std::string _electionName, _choiceName;
    std::cout << "Input election name:";
    std::cin >> _electionName;
    std::cout << "Input choice name:";
    std::cin >> _choiceName;
    vote.set_election_name(_electionName);
    vote.set_choice_name(_choiceName);
    voting::AuthToken *tok = new voting::AuthToken();
    tok->set_value(token.value());
    vote.set_allocated_token(tok);

    // RPC
    Status status = stub_->CastVote(&context, vote, &vstatus);
    if (status.error_code() == 14)
    {
      if (!isbackup)
        connectBackup();
      ClientContext newContext;
      status = backupStub_->CastVote(&newContext, vote, &vstatus);
    }
    if (!status.ok())
    {
      std::cout << "CastVote RPC failed" << std::endl;
    }
    else if (vstatus.code() == 0)
    {
      std::cout << "CastVote successed" << std::endl;
    }
    else if (vstatus.code() == 1)
    {
      std::cout << "Invalid authentication token" << std::endl;
    }
    else if (vstatus.code() == 2)
    {
      std::cout << "Invalid election name" << std::endl;
    }
    else if (vstatus.code() == 3)
    {
      std::cout << "The voter's group is not allowed in the election" << std::endl;
    }
    else if (vstatus.code() == 4)
    {
      std::cout << "A previous vote has been cast" << std::endl;
    }
    return;
  }

  // rpc GetResult(ElectionName) returns (ElectionResult)
  void GetResult()
  {
    std::string name;
    std::cout << "Get the election result. Input the election name:";
    std::cin >> name;
    ElectionName electionname;
    electionname.set_name(name);
    ClientContext context;
    ElectionResult result;
    // RPC
    Status status = stub_->GetResult(&context, electionname, &result);
    if (status.error_code() == 14)
    {
      if (!isbackup)
        connectBackup();
      ClientContext newContext;
      status = backupStub_->GetResult(&newContext, electionname, &result);
    }
    if (!status.ok())
    {
      std::cout << "GetResult RPC failed" << std::endl;
      return;
    }
    if (result.status() == 0)
    {
      std::cout << "GetResult successed" << std::endl;
      for (int i = 0; i < result.counts_size(); i++)
      {
        std::cout << result.counts(i).choice_name() << " : "
                  << result.counts(i).count() << std::endl;
      }
    }
    else if (result.status() == 1)
    {
      std::cout << "Non-existent election" << std::endl;
    }
    else if (result.status() == 2)
    {
      std::cout << "The election is still ongoing. Election result is not available yet." << std::endl;
    }
  }

  void login()
  {
    std::cout << "This is eVoting cilent login system." << std::endl;
    std::string challenge;
    int pass = 1;
    try
    {
      challenge = preAuth();
      while (challenge == "RPC failed")
        challenge = preAuth();
      pass = Auth(challenge);
    }
    catch (const std::exception &e)
    {
      std::cerr << e.what() << '\n';
      return;
    }

    if (pass == 0)
      controller();
    else
      std::cout << "login fail, try again\n";
    std::cout << "-----------------------------------------" << std::endl;
    login();
  }

  void controller()
  {
    std::string str;
    std::string instruction("\t 'election' or 'e' to create an election\n\t 'vote' or 'v' to cast a vote\n\t 'get' or 'g' to get the election result\n\t 'help' or 'h' to show the instruction\n\t 'logout' to logout\n\t 'exit' to exit the system\n");
    std::cout << instruction << "% ";
    while (std::cin >> str)
    {
      if (str == "exit")
        exit(0);
      else if (str == "help" || str == "h")
        std::cout << instruction;
      else if (str == "election" || str == "e") // create election
        CreateElection();
      else if (str == "vote" || str == "v") // cast vote
        CastVote();
      else if (str == "get" || str == "g") // get result
        GetResult();
      else if (str == "logout")
      {
        token.set_value("");
        voter_name = "";
        break;
      }
      else
        std::cout << "invalid command\n";
      std::cout << "% ";
    }
  }

private:
  std::unique_ptr<eVoting::Stub> stub_;

  // is backup connect, connect is 1, otherwise 0
  int isbackup;
  std::unique_ptr<eVoting::Stub> backupStub_;
  std::shared_ptr<Channel> backupChannel;

  AuthToken token;
  std::string voter_name;
};

int main(int argc, char **argv)
{
  server_address = "0.0.0.0:50051";
  backup_address = "0.0.0.0:50052";
  if (argc == 2)
  {
    server_address = argv[1];
  }
  else if (argc == 3)
  {
    server_address = argv[1];
    backup_address = argv[2];
  }

  auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
  VotingClient voter(channel);

  std::cout << "-------------- eVoting --------------" << std::endl;
  std::cout << "connect to server(" << server_address << ")" << std::endl;
  voter.login();

  return 0;
}
