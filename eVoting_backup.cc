#include "eVoting_backup.h"

#define DBCONNECTINFO "dbname=eVoting user=admin password=passwd hostaddr=127.0.0.1 port=8002"

int isSync;
int logid;
pqxx::connection *dbConnect;
ServerConnection *GRPCconnect;
std::string local_address;
std::string remote_address;
pthread_barrier_t barrier;

// save the write DB log
std::vector<struct log> loglist;

int checkVoterExist(std::string name)
{
  // check db exist the name
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string sql("SELECT name FROM Voter WHERE name = '" + name + "';");

  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 2;
  }
  tx.commit();
  c.disconnect();

  if (r.size() != 0)
  {
    // std::cout << "has same voter name\n";
    return 1;
  }
  return 0;
}

int checkElectionExist(std::string name)
{
  // check db exist the name
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string sql("SELECT name FROM Election WHERE name = '" + name + "';");

  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 2;
  }
  tx.commit();
  c.disconnect();

  if (r.size() != 0)
  {
    // std::cout << "has same Election name\n";
    return 1;
  }
  return 0;
}

ServerConnection::ServerConnection()
{
  remoteChannel = grpc::CreateChannel(remote_address, grpc::InsecureChannelCredentials());
  remoteStub_ = eVoting::NewStub(remoteChannel);
  // std::cout << "building connection" << std::endl;
}

ServerConnection::ServerConnection(std::shared_ptr<Channel> channel) : remoteStub_(eVoting::NewStub(channel))
{
  // std::cout << "building connection" << std::endl;
}

eVotingServer::eVotingServer()
{
}

voting::Status localRegisterVoter(Voter _voter)
{
  voting::Status vstatus;
  std::string name(_voter.name());

  // check db exist the name
  int exist = checkVoterExist(name);
  if (exist == 0)
  {
    // add user to db
    pqxx::connection c(DBCONNECTINFO);
    pqxx::result r;
    pqxx::work tx(c);
    c.prepare("add_user", "INSERT INTO Voter(name , groups , public_key) VALUES ($1, $2, $3)");
    std::string pk_str = _voter.public_key();
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    for (int i = 0; i < crypto_sign_PUBLICKEYBYTES; i++)
    {
      pk[i] = pk_str[i];
    }

    pqxx::binarystring blob(pk, crypto_sign_PUBLICKEYBYTES);
    try
    {
      r = tx.exec_prepared("add_user", name, _voter.group(), blob);
    }
    catch (const std::exception &e)
    {
      c.disconnect();
      std::cerr << e.what() << '\n';
      vstatus.set_code(2);
      return vstatus;
    }
    tx.commit();
    c.disconnect();
    vstatus.set_code(0);
    return vstatus;
  }
  else if (exist == 1)
  {
    vstatus.set_code(1);
    return vstatus;
  }

  // exist == 2 other error
  vstatus.set_code(2);
  return vstatus;
}

voting::Status localUnregisterVoter(VoterName _voterName)
{
  voting::Status vstatus;
  std::string name = _voterName.name();
  // check db exist the name
  int exist = checkVoterExist(name);
  if (exist == 1)
  {
    // delete Voter from db by VoterName
    std::string insert_val("'" + name + "'");
    std::string sql("DELETE FROM Voter WHERE name = " + insert_val + ";");
    pqxx::connection c(DBCONNECTINFO);
    pqxx::result r;
    pqxx::work tx(c);
    try
    {
      r = tx.exec(sql);
    }
    catch (const std::exception &e)
    {
      c.disconnect();
      std::cerr << e.what() << '\n';
      vstatus.set_code(2);
      return vstatus;
    }
    tx.commit();
    c.disconnect();
    vstatus.set_code(0);
    return vstatus;
  }
  else if (exist == 0)
  {
    // No voter with the name exists on the server
    vstatus.set_code(1);
    return vstatus;
  }
  // exist == 2 other error
  vstatus.set_code(2);
  return vstatus;
}

Status eVotingServer::RegisterVoter(ServerContext *context, const Voter *voter, voting::Status *status)
{
  std::string name(voter->name());
  std::cout << "recv RegisterVoter " << name << std::endl;
  // check db exist the name
  int exist = checkVoterExist(name);
  if (exist == 1)
  {
    status->set_code(1);
    return Status::OK;
  }
  else if (exist == 2)
  {
    // exist == 2 other error
    status->set_code(2);
    return Status::OK;
  }
  // add user to db
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);

  c.prepare("add_user", "INSERT INTO Voter(name , groups , public_key) VALUES ($1, $2, $3)");

  std::string pk_str = voter->public_key();
  unsigned char *pk;
  pk = (unsigned char *)pk_str.c_str();
  pqxx::binarystring blob(pk, crypto_sign_PUBLICKEYBYTES);

  try
  {
    r = tx.exec_prepared("add_user", name, voter->group(), blob);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    status->set_code(2);
    return Status::OK;
  }
  // write to log
  struct log temp;
  temp.id = ++logid;
  temp.target = "RegisterVoter";
  temp.parameter.push_back(name);
  loglist.push_back(temp);
  writeLog(temp);

  tx.commit();
  c.disconnect();
  status->set_code(0);
  return Status::OK;
}

Status eVotingServer::UnregisterVoter(ServerContext *context, const VoterName *voterName, voting::Status *status)
{
  std::string name = voterName->name();
  std::cout << "recv unRegisterVoter " << name << std::endl;
  // check db exist the name
  int exist = checkVoterExist(name);
  if (exist == 1)
  {
    // delete Voter from db by VoterName
    std::string insert_val("'" + name + "'");
    std::string sql("DELETE FROM Voter WHERE name = " + insert_val + ";");
    pqxx::connection c(DBCONNECTINFO);
    pqxx::result r;
    pqxx::work tx(c);
    try
    {
      r = tx.exec(sql);
    }
    catch (const std::exception &e)
    {
      c.disconnect();
      std::cerr << e.what() << '\n';
      status->set_code(2);
      return Status::OK;
    }
    // write to log
    struct log temp;
    temp.id = ++logid;
    temp.target = "UnregisterVoter";
    temp.parameter.push_back(name);
    loglist.push_back(temp);
    writeLog(temp);
    tx.commit();
    c.disconnect();
    status->set_code(0);
    return Status::OK;
  }
  else if (exist == 0)
  {
    // No voter with the name exists on the server
    status->set_code(1);
    return Status::OK;
  }
  // exist == 2 other error
  status->set_code(2);
  return Status::OK;
}

int eVotingServer::searchAuthByName(std::string _name)
{
  for (int i = 0; i < userlist.size(); i++)
  {
    if (_name == userlist[i].name)
      return i;
  }
  return -1;
}

bool eVotingServer::isAuth(std::string _token)
{
  if (_token == "server sync data")
    return true;
  std::time_t nowtime = time(NULL);
  for (int i = 0; i < userlist.size(); i++)
  {
    if (_token == userlist[i].token)
    {
      if (nowtime - userlist[i].startTime > 3600)
      {
        userlist.erase(userlist.begin() + i);
        return false;
      }
      return true;
    }
  }
  return false;
}

Status eVotingServer::PreAuth(ServerContext *context, const VoterName *voterName, Challenge *challenge)
{
  std::string name = voterName->name();
  // use name to find db Voter
  int exist = checkVoterExist(name);
  if (exist != 1)
    return Status::CANCELLED;
  srand(time(NULL));
  int x = rand();
  std::string message(name + std::to_string(x));
  challenge->set_value(message);

  bool existlist = 0;
  authStruct temp;
  temp.name = name;
  temp.challenge = message;
  temp.startTime = time(NULL);
  for (int i = 0; i < userlist.size(); i++)
  {
    if (name == userlist[i].name)
    {
      userlist[i].challenge = message;
      userlist[i].startTime = time(NULL);
      existlist = 1;
    }
  }
  if (existlist == 0)
    userlist.push_back(temp);
  return Status::OK;
}

Status eVotingServer::Auth(ServerContext *context, const AuthRequest *authRequest, AuthToken *authToken)
{
  bool is_pass = 0;
  std::string restmp = authRequest->response().value();
  unsigned char *res = (unsigned char *)restmp.c_str();
  std::string _name = authRequest->name().name();

  int index = searchAuthByName(_name);
  if (index == -1)
    return Status::CANCELLED;
  char pktmp[crypto_sign_PUBLICKEYBYTES];

  pqxx::connection conn(DBCONNECTINFO);
  pqxx::work work(conn);
  pqxx::result result = work.exec("SELECT public_key FROM Voter");
  pqxx::field field = result[0][0];

  pqxx::binarystring blob(field);

  unsigned char pk[crypto_sign_PUBLICKEYBYTES];
  for (size_t i = 0; i < crypto_sign_PUBLICKEYBYTES; i++)
  {
    pk[i] = blob[i];
  }

  std::string mestmp = userlist[index].challenge;
  unsigned char *mes = (unsigned char *)mestmp.c_str();
  unsigned long long len = (unsigned long long)mestmp.length();
  if (crypto_sign_verify_detached(res, mes, len, pk) == 0)
  {
    // std::cout<<"by db pass"<<std::endl;
    is_pass = 1;
  }
  else
  {
    std::string fileName("key/" + _name + "pk");
    unsigned char pkfile[crypto_sign_PUBLICKEYBYTES];
    // using file to test
    std::fstream file;
    file.open(fileName, std::fstream::in | std::fstream::binary);
    if (file.is_open() != 0)
    {
      // std::cout << fileName << " pubilc key open file fail\n";
      file.read((char *)pkfile, crypto_sign_PUBLICKEYBYTES);
      file.close();
      if (crypto_sign_verify_detached(res, mes, len, pkfile) == 0)
      {
        is_pass = 1;
      }
    }
    else
    {
      std::cout << "verify sign fail\n";
    }
  }

  if(is_pass)
  {
    userlist[index].startTime = time(NULL);
    srand(time(NULL));
    int x = rand();
    std::string passToken = std::to_string(x);
    int i = 0;
    while (i < userlist.size())
    {
      if (passToken == userlist[i].token)
      {
        srand(time(NULL));
        x = rand();
        passToken = std::to_string(x);
        i = 0;
      }
      i++;
    }

    userlist[index].token = passToken;
    authToken->set_value(passToken);
    // send to backup server
    ClientContext remotecontext;
    voting::User User;
    std::string t_token = userlist[index].token;
    std::time_t time_startTime = userlist[index].startTime;
    voting::AuthToken *tok = new voting::AuthToken();
    tok->set_value(t_token);
    Timestamp *t_startTime = new Timestamp();
    t_startTime->set_seconds(time_startTime);
    t_startTime->set_nanos(0);
    User.add_name(_name);
    User.add_token()->CopyFrom(*tok);
    User.add_start_time()->CopyFrom(*t_startTime);
    voting::Status remoteVstatus;
    Status remoteStatus = GRPCconnect->remoteStub_->SyncAuth(&remotecontext, User, &remoteVstatus);
  }

  return Status::OK;
}

Status eVotingServer::CreateElection(ServerContext *context, const Election *election, voting::Status *status)
{
  std::cout << "recv election" << std::endl;
  // check token
  std::string token = election->token().value();
  if (!isAuth(token))
  {
    status->set_code(1);
    return Status::OK;
  }

  // Missing groups or choices specification
  if (election->groups_size() == 0 || election->choices_size() == 0)
  {
    status->set_code(2);
    return Status::OK;
  }
  std::string name = election->name();
  int exist = checkElectionExist(name);
  if (exist == 1)
  {
    std::cout << "same election name" << std::endl;
    status->set_code(3);
    return Status::OK;
  }

  std::string group_str = "";
  for (int i = 0; i < election->groups_size(); i++)
  {
    group_str += election->groups(i);
    group_str += " ";
  }

  int end_date = election->end_date().seconds();
  std::string choices_val = "";
  for (int i = 0; i < election->choices_size(); i++)
  {
    choices_val += election->choices(i) + " ";
  }
  // INSERT INTO Election(name , groups , end_date) VALUES ( 'name' , 'groups' , end_date );
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string insert_val("'" + name + "' , '" + group_str + "' , '" + choices_val + "' , " + std::to_string(end_date));
  std::string sql("INSERT INTO Election (name , groups , choices , end_date) VALUES (" + insert_val + ");");

  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    status->set_code(3);
    return Status::OK;
  }

  // CREATE TABLE electionName ...
  pqxx::connection c2(DBCONNECTINFO);
  pqxx::result r2;
  pqxx::work tx2(c2);
  std::string create_val = "";
  for (int i = 0; i < election->choices_size(); i++)
  {
    create_val += election->choices(i);
    create_val += " INTEGER ,";
  }

  std::string sql2("CREATE TABLE " + name + " (" + create_val + " voter VARCHAR(20));");
  try
  {
    r2 = tx2.exec(sql2);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    c2.disconnect();
    std::cerr << e.what() << '\n';
    status->set_code(3);
    return Status::OK;
  }

  // send to backup
  Election reElection = *election;
  voting::AuthToken *tok = new voting::AuthToken();
  tok->set_value("server sync data");
  reElection.set_allocated_token(tok);
  // if (isSync == 0)
  // {
  //   ClientContext remotecontext;
  //   voting::Status remoteVstatus;
  //   Status remoteStatus = GRPCconnect->remoteStub_->CreateElection(&remotecontext, reElection, &remoteVstatus);
  // }
  // write to log
  struct log temp;
  temp.id = ++logid;
  temp.target = "CreateElection";
  temp.parameter.push_back(name);
  writeLog(temp);
  tx.commit();
  tx2.commit();
  c.disconnect();
  c2.disconnect();
  status->set_code(0);
  return Status::OK;
}

Status eVotingServer::CastVote(ServerContext *context, const Vote *vote, voting::Status *status)
{
  std::cout << "recv CastVote" << std::endl;
  // check token
  std::string token = vote->token().value();
  if (!isAuth(token))
  {
    status->set_code(1);
    return Status::OK;
  }

  // SELECT electionName FROM electionTable
  std::string election_name = vote->election_name();
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string sql("SELECT * FROM Election WHERE name = '" + election_name + "';");
  // std::cout << "sql:" << sql << std::endl;
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return Status::CANCELLED;
  }

  // Invalid election name
  if (r.size() == 0)
  {
    std::cout << "Invalid election name" << std::endl;
    c.disconnect();
    status->set_code(2);
    return Status::OK;
  }

  std::string electionGroups;
  std::time_t endtime;

  pqxx::field field = r[0][1];
  electionGroups = field.c_str();
  // std::cout << "groups:" << electionGroups << std::endl;

  field = r[0][3];
  std::string temp_str = field.c_str();
  endtime = std::stoll(temp_str);

  // election is not ongoing
  if (time(NULL) - endtime > 0)
  {
    // std::cout << "time diff:" << time(NULL) - endtime << std::endl;
    c.disconnect();
    status->set_code(2);
    return Status::OK;
  }
  tx.commit();
  c.disconnect();

  // get voter name
  std::string voter_name = "";
  for (int i = 0; i < userlist.size(); i++)
  {
    if (token == userlist[i].token)
    {
      voter_name = userlist[i].name;
      break;
    }
  }

  // get voter group
  pqxx::connection c2(DBCONNECTINFO);
  pqxx::result r2;
  pqxx::work tx2(c2);
  sql = "SELECT groups FROM Voter WHERE name = '" + voter_name + "';";
  try
  {
    r2 = tx2.exec(sql);
  }
  catch (const std::exception &e)
  {
    c2.disconnect();
    std::cerr << e.what() << '\n';
    return Status::CANCELLED;
  }

  field = r2[0][0];
  temp_str = field.c_str();
  std::vector<std::string> voter_group;
  std::stringstream s2(temp_str);
  while (true)
  {
    std::string tmp;
    s2 >> tmp;
    if (s2.fail())
      break;
    // std::cout << "group:" << tmp;
    voter_group.push_back(tmp);
  }
  // std::cout << std::endl;
  tx2.commit();
  c2.disconnect();

  // check group
  bool groupFlag = false;
  std::stringstream s1(electionGroups);
  while (true)
  {
    std::string tmp;
    s1 >> tmp;
    if (s1.fail())
      break;
    for (size_t i = 0; i < voter_group.size(); i++)
    {
      if (tmp == voter_group[i])
        groupFlag = true;
    }
  }
  if (!groupFlag)
  {
    // Status.code=3 : The voter’s group is not allowed in the election
    status->set_code(3);
    return Status::OK;
  }

  // check previous vote
  pqxx::connection c3(DBCONNECTINFO);
  sql = "SELECT Voter FROM " + election_name + ";";
  pqxx::result r3;
  pqxx::work tx3(c3);
  try
  {
    r3 = tx3.exec(sql);
  }
  catch (const std::exception &e)
  {
    c3.disconnect();
    std::cerr << e.what() << '\n';
    return Status::OK;
  }

  const int num_rows = r3.size();
  for (int rownum = 0; rownum < num_rows; ++rownum)
  {
    const pqxx::row row = r3[rownum];
    const int num_cols = row.size();
    for (int colnum = 0; colnum < num_cols; ++colnum)
    {
      const pqxx::field field = row[colnum];
      if (std::string(field.c_str()) == voter_name)
      {
        // previous vote
        status->set_code(4);
        return Status::OK;
      }
    }
  }
  tx3.commit();
  c3.disconnect();
  // SELECT voter_name FROM electionName WHERE voter_name == name

  // INSERT INTO {election_name}( {choice_name} , Voter)  VALUES ( 1 , {voter_name} );
  pqxx::connection c4(DBCONNECTINFO);
  pqxx::result r4;
  pqxx::work tx4(c4);
  std::string choice_name = vote->choice_name();
  sql = "INSERT INTO " + election_name + " ( " + choice_name + " , Voter )  VALUES ( 1 , '" + voter_name + "' );";
  // std::cout << sql << std::endl;
  try
  {
    r4 = tx4.exec(sql);
  }
  catch (const std::exception &e)
  {
    c4.disconnect();
    std::cerr << e.what() << '\n';
    return Status::CANCELLED;
  }
  // // send to backup
  // if (isSync == 0)
  // {
  //   Vote v = *vote;
  //   ClientContext remotecontext;
  //   voting::Status remoteVstatus;
  //   voting::AuthToken *tok = new voting::AuthToken();
  //   tok->set_value("server sync data");
  //   v.set_allocated_token(tok);
  //   Status remoteStatus = GRPCconnect->remoteStub_->CastVote(&remotecontext, v, &remoteVstatus);
  // }

  struct log temp;
  temp.id = ++logid;
  temp.target = "CastVote";
  temp.parameter.push_back(voter_name);
  temp.parameter.push_back(election_name);
  writeLog(temp);

  tx4.commit();
  c4.disconnect();
  status->set_code(0);
  return Status::OK;
}

Status eVotingServer::GetResult(ServerContext *context, const ElectionName *electionName, ElectionResult *electionResult)
{
  // SELECT electionName FROM electionTable checkElectionExist
  // exist , ongoing , or OK
  std::string election_name = electionName->name();
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string sql("SELECT choices , end_date FROM Election WHERE name = '" + election_name + "';");
  // std::cout << "sql:" << sql << std::endl;
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    electionResult->set_status(1);
    return Status::OK;
  }
  tx.commit();
  c.disconnect();

  // ElectionResult.status = 1: Non-existent election
  if (r.size() == 0)
  {
    std::cout << "electionName not exist\n";
    electionResult->set_status(1);
    return Status::OK;
  }

  // check the election is still ongoing.
  std::time_t endtime;
  pqxx::field field = r[0][1];
  std::string temp_str = field.c_str();
  endtime = std::stoll(temp_str);
  // std::cout << "time different:" << time(NULL) - endtime << std::endl;
  if (time(NULL) - endtime < 0)
  {
    std::cout << "election is still ongoing\n";
    electionResult->set_status(2);
    return Status::OK;
  }

  // get choice_name
  std::vector<std::string> choicesList;
  field = r[0][0];
  temp_str = field.c_str();
  std::stringstream s2(temp_str);
  while (true)
  {
    std::string tmp;
    s2 >> tmp;
    if (s2.fail())
      break;
    // std::cout << "choice_name:" << tmp;
    choicesList.push_back(tmp);
  }

  // get vote count FROM electionName
  pqxx::connection c2(DBCONNECTINFO);
  pqxx::result r2;
  pqxx::work tx2(c2);
  sql = "SELECT * FROM " + election_name + ";";
  // std::cout << "sql:" << sql << std::endl;
  try
  {
    r2 = tx2.exec(sql);
  }
  catch (const std::exception &e)
  {
    c2.disconnect();
    std::cerr << e.what() << '\n';
    return Status::CANCELLED;
  }
  tx2.commit();
  c2.disconnect();

  // list of choices counts
  std::vector<int> countsList;
  int num_rows = r2.size();
  const pqxx::row tmp_row = r2[num_rows];
  int countslen = tmp_row.size() - 1;
  for (int i = 0; i < countslen; i++)
  {
    countsList.push_back(0);
  }

  for (int rownum = 0; rownum < num_rows; ++rownum)
  {
    const pqxx::row row = r2[rownum];
    const int num_cols = row.size();
    for (int colnum = 0; colnum < num_cols - 1; ++colnum)
    {
      const pqxx::field field = row[colnum];
      temp_str = field.c_str();
      // std::cout << temp_str << '\t';
      if (temp_str == "1")
        countsList[colnum]++;
    }
  }
  VoteCount *vc;
  for (int i = 0; i < countslen; i++)
  {
    vc = new VoteCount();
    vc->set_choice_name(choicesList[i]);
    vc->set_count(countsList[i]);
    electionResult->add_counts()->CopyFrom(*vc);
  }
  electionResult->set_status(0);
  return Status::OK;
}

void synRegisterVoter(std::string name)
{
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string sql("SELECT groups, public_key FROM Voter WHERE name = '" + name + "';");

  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
  }
  tx.commit();
  c.disconnect();

  if (r.size() == 0)
    return;
  ClientContext context;
  voting::Status vstatus;
  std::string str = r[0][0].c_str();
  Voter voter;
  voter.set_name(name);
  voter.set_group(str);
  pqxx::binarystring blob(r[0][1]);

  unsigned char pk[crypto_sign_PUBLICKEYBYTES];
  for (size_t i = 0; i < crypto_sign_PUBLICKEYBYTES; i++)
  {
    pk[i] = blob[i];
  }
  voter.set_public_key(pk, crypto_sign_PUBLICKEYBYTES);
  Status status = GRPCconnect->remoteStub_->RegisterVoter(&context, voter, &vstatus);
}

void synUnregisterVoter(std::string name)
{
  ClientContext context;
  voting::Status vstatus;
  VoterName voterName;
  voterName.set_name(name);
  Status status = GRPCconnect->remoteStub_->UnregisterVoter(&context, voterName, &vstatus);
}

void synCreateElection(std::string name)
{
  pqxx::connection c(DBCONNECTINFO);
  pqxx::result r;
  pqxx::work tx(c);
  std::string sql("SELECT * FROM Election WHERE name = '" + name + "';");

  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return;
  }
  tx.commit();
  c.disconnect();

  std::string group = r[0][1].c_str();
  std::string choices = r[0][2].c_str();
  std::string end_date = r[0][3].c_str();

  // split _groups, _choiceName
  std::vector<std::string> grouplist, choicelist;
  std::stringstream s1(group);
  std::stringstream s2(choices);
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
  time_t endtime = std::stoi(end_date);

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

  election->set_name(name);
  voting::AuthToken *tok = new voting::AuthToken();
  tok->set_value("server sync data");
  election->set_allocated_token(tok);

  Timestamp *end_time = new Timestamp();
  end_time->set_seconds(endtime);
  end_time->set_nanos(0);
  election->set_allocated_end_date(end_time);
  Status status = GRPCconnect->remoteStub_->CreateElection(&context, *election, &vstatus);
}

void synCastVote(std::string name, std::string election)
{
  std::cout << "syn vote:" << name << " in " << election << std::endl;
  std::string _electionName, _choiceName, _voterName;
  _voterName = name;
  _electionName = election;
  pqxx::connection c(DBCONNECTINFO);
  std::string sql("SELECT * FROM " + election + " WHERE voter = '" + name + "';");
  pqxx::result r;
  pqxx::work tx(c);
  // std::cout << "sql1: " << sql << std::endl;
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return;
  }

  tx.commit();
  c.disconnect();

  pqxx::connection c2(DBCONNECTINFO);
  pqxx::result r2;
  pqxx::work tx2(c2);
  std::string sql2("SELECT choices FROM Election WHERE name = '" + election + "';");
  // std::cout << "sql2: " << sql2 << std::endl;
  try
  {
    r2 = tx2.exec(sql2);
  }
  catch (const std::exception &e)
  {
    c2.disconnect();
    std::cerr << e.what() << '\n';
    return;
  }
  tx2.commit();
  c2.disconnect();

  // get choices list
  std::string choices = r2[0][0].c_str();
  std::stringstream s(choices);
  std::vector<std::string> choicelist;
  std::string tmp;
  // std::cout << "choices: ";
  while (true)
  {
    s >> tmp;
    if (s.fail())
      break;
    choicelist.push_back(tmp);
    // std::cout << tmp << " ";
  }
  // std::cout << std::endl;
  // get choices
  _choiceName = "";
  const int num_cols = r[0].size();
  for (int num = 0; num < num_cols; ++num)
  {
    pqxx::field field = r[0][num];
    std::string temp(field.c_str());
    // std::cout << temp << " ";
    if (temp == "1")
    {
      // std::cout << "choices name:" << choicelist[rownum] << std::endl;
      _choiceName = choicelist[num];
    }
  }

  ClientContext context;
  ServerVote serverVote;
  voting::Status vstatus;

  serverVote.set_voter_name(_voterName);
  serverVote.set_election_name(_electionName);
  serverVote.set_choice_name(_choiceName);

  // RPC
  Status status = GRPCconnect->remoteStub_->SyncVote(&context, serverVote, &vstatus);
}

Status eVotingServer::SyncDb(ServerContext *context, const voting::Status *status, voting::Status *restatus)
{
  int remoteId = status->code();
  int localId = readLogId();
  if (localId > remoteId)
  {
    pqxx::connection c(DBCONNECTINFO);
    std::string sql("SELECT * FROM LogTable WHERE id > " + std::to_string(remoteId) + ";");
    pqxx::result r;
    pqxx::work tx(c);
    try
    {
      r = tx.exec(sql);
    }
    catch (const std::exception &e)
    {
      c.disconnect();
      std::cerr << e.what() << '\n';
      restatus->set_code(-1);
      return Status::CANCELLED;
    }

    const int num_rows = r.size();
    if (num_rows == 0)
      std::cout << "query empty" << std::endl;
    for (int rownum = 0; rownum < num_rows; ++rownum)
    {
      std::string target;
      std::string name;
      std::string election = "";
      const pqxx::row row = r[rownum];
      const int num_cols = row.size();
      for (int colnum = 0; colnum < num_cols; ++colnum)
      {
        const pqxx::field field = row[colnum];
        std::cout << field.c_str() << '\t';
      }
      std::cout << std::endl;
      target = row[1].c_str();
      name = row[2].c_str();
      election = row[3].c_str();
      std::cout << target << " " << name << " " << election << std::endl;

      if (target == "RegisterVoter")
      {
        synRegisterVoter(name);
      }
      else if (target == "UnregisterVoter")
      {
        synUnregisterVoter(name);
      }
      else if (target == "CreateElection")
      {
        synCreateElection(name);
      }
      else if (target == "CastVote")
      {
        synCastVote(name, election);
      }
    }
    tx.commit();
    c.disconnect();
  }

  restatus->set_code(localId);
  return Status::OK;
}

Status eVotingServer::SyncAuth(ServerContext *context, const voting::User *user, voting::Status *status)
{
  for (size_t i = 0; i < user->name_size(); i++)
  {
    authStruct t;
    t.name = user->name(i);
    t.token = user->token(i).value();
    t.startTime = user->start_time(i).seconds();
    std::cout << "recv server login name:" << t.name << std::endl;
    userlist.push_back(t);
  }
  status->set_code(0);
  return Status::OK;
}

Status eVotingServer::SyncUser(ServerContext *context, const voting::Status *status, voting::User *User)
{
  if (status->code() == 0)
    for (size_t i = 0; i < userlist.size(); i++)
    {
      std::string t_name = userlist[i].name;
      std::string t_token = userlist[i].token;
      std::time_t time_startTime = userlist[i].startTime;
      voting::AuthToken *tok = new voting::AuthToken();
      tok->set_value(t_token);
      Timestamp *t_startTime = new Timestamp();
      t_startTime->set_seconds(time_startTime);
      t_startTime->set_nanos(0);
      User->add_name(t_name);
      User->add_token()->CopyFrom(*tok);
      User->add_start_time()->CopyFrom(*t_startTime);
    }
  return Status::OK;
}

Status eVotingServer::SyncVote(ServerContext *context, const voting::ServerVote *serverVote, voting::Status *status)
{
  std::string voter_name = serverVote->voter_name();
  std::string election_name = serverVote->election_name();
  std::string choice_name = serverVote->choice_name();
  std::cout << "recv sync vote " << election_name << " " << choice_name << " by " << voter_name << std::endl;
  // check previous vote
  pqxx::connection c3(DBCONNECTINFO);
  std::string sql = "SELECT Voter FROM " + election_name + ";";
  pqxx::result r3;
  pqxx::work tx3(c3);
  try
  {
    r3 = tx3.exec(sql);
  }
  catch (const std::exception &e)
  {
    c3.disconnect();
    std::cerr << e.what() << '\n';
    return Status::OK;
  }

  const int num_rows = r3.size();
  for (int rownum = 0; rownum < num_rows; ++rownum)
  {
    const pqxx::row row = r3[rownum];
    const int num_cols = row.size();
    for (int colnum = 0; colnum < num_cols; ++colnum)
    {
      const pqxx::field field = row[colnum];
      if (std::string(field.c_str()) == voter_name)
      {
        // previous vote
        tx3.commit();
        c3.disconnect();
        status->set_code(0);
        return Status::OK;
      }
    }
  }
  tx3.commit();
  c3.disconnect();

  // INSERT INTO {election_name}( {choice_name} , Voter)  VALUES ( 1 , {voter_name} );
  pqxx::connection c4(DBCONNECTINFO);
  pqxx::result r4;
  pqxx::work tx4(c4);
  sql = "INSERT INTO " + election_name + " ( " + choice_name + " , Voter )  VALUES ( 1 , '" + voter_name + "' );";
  // std::cout << sql << std::endl;
  try
  {
    r4 = tx4.exec(sql);
  }
  catch (const std::exception &e)
  {
    c4.disconnect();
    std::cerr << e.what() << '\n';
    return Status::CANCELLED;
  }

  struct log temp;
  temp.id = ++logid;
  temp.target = "CastVote";
  temp.parameter.push_back(voter_name);
  temp.parameter.push_back(election_name);
  writeLog(temp);
  tx4.commit();
  c4.disconnect();

  status->set_code(0);
  return Status::OK;
}

void eVotingServer::synServer()
{
  isSync = 1;
  auto channel = grpc::CreateChannel(remote_address, grpc::InsecureChannelCredentials());
  GRPCconnect = new ServerConnection(channel);
  // run log send by remote server
  authStruct t;
  t.name = "server";
  t.token = "server sync data";
  t.startTime = time(NULL);
  userlist.push_back(t);

  ClientContext dbcontext;
  voting::Status dbVstatus;
  dbVstatus.set_code(logid);
  voting::Status reVstatus;
  std::cout << "sync DB... ";
  Status dbStatus = GRPCconnect->remoteStub_->SyncDb(&dbcontext, dbVstatus, &reVstatus);
  if (!dbStatus.ok())
    std::cout << " RPC failed " << std::endl;
  else
  {
    std::cout << "complete" << std::endl;
    logid = readLogId();
    if (logid < reVstatus.code())
      logid = reVstatus.code();
  }
  // std::cout << "fin logid : " << logid << std::endl;

  // sync auth status
  ClientContext authcontext;
  voting::Status authVstatus;
  authVstatus.set_code(0);
  voting::User loginlist;
  std::cout << "sync login user list... ";
  Status authStatus = GRPCconnect->remoteStub_->SyncUser(&authcontext, authVstatus, &loginlist);
  for (size_t i = 0; i < loginlist.name_size(); i++)
  {
    authStruct t;
    t.name = loginlist.name(i);
    t.token = loginlist.token(i).value();
    t.startTime = loginlist.start_time(i).seconds();
    userlist.push_back(t);
  }
  if (!authStatus.ok())
    std::cout << " RPC failed " << std::endl;
  else
    std::cout << "complete" << std::endl;
  userlist.erase(userlist.begin());
  isSync = 0;
}

void *RunServer(void *)
{
  std::cout << "wait for Synchronize... " << std::endl;
  std::string server_address = local_address;
  eVotingServer service;
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  sleep(5);
  service.synServer();
  std::cout << "Server listening on " << server_address << std::endl;
  pthread_barrier_wait(&barrier);
  server->Wait();
  pthread_exit(NULL);
}

void writeLog(const struct log &data)
{
  int id = data.id;
  std::string target = data.target;
  std::string name = data.parameter[0];
  pqxx::connection c(DBCONNECTINFO);
  // pqxx::result r;
  pqxx::work tx(c);
  try
  {
    if (target == "RegisterVoter")
    {
      c.prepare("RegisterVoter", "INSERT INTO LogTable(id, target, name) VALUES ($1, $2, $3)");
      tx.exec_prepared("RegisterVoter", id, target, name);
    }
    else if (target == "UnregisterVoter")
    {
      c.prepare("UnregisterVoter", "INSERT INTO LogTable(id, target, name) VALUES ($1, $2, $3)");
      tx.exec_prepared("UnregisterVoter", id, target, name);
    }
    else if (target == "CreateElection")
    {
      c.prepare("CreateElection", "INSERT INTO LogTable(id, target, name) VALUES ($1, $2, $3)");
      tx.exec_prepared("CreateElection", id, target, name);
    }
    else if (target == "CastVote")
    {
      std::string election = data.parameter[1];
      c.prepare("CastVote", "INSERT INTO LogTable(id, target, name, election) VALUES ($1, $2, $3, $4)");
      tx.exec_prepared("CastVote", id, target, name, election);
    }
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return;
  }
  tx.commit();
  c.disconnect();
}

// struct log readLog(int id)
// {
//   struct log temp;
//   temp.id = -1;
//   std::string strId = std::to_string(id);
//   pqxx::connection c(DBCONNECTINFO);
//   std::string sql("SELECT * FROM LogTable WHERE id = " + strId + ";");
//   pqxx::result r;
//   pqxx::work tx(c);
//   try
//   {
//     r = tx.exec(sql);
//   }
//   catch (const std::exception &e)
//   {
//     c.disconnect();
//     std::cerr << e.what() << '\n';
//     return temp;
//   }
//   const int num_rows = r.size();
//   if (num_rows == 0)
//     std::cout << "query empty" << std::endl;

//   const pqxx::row row = r[0];
//   const int num_cols = row.size();
//   if (num_cols > 0)
//   {
//     temp.id = std::stoi(r[0][0].c_str());
//     temp.target = r[0][1].c_str();
//     temp.parameter.push_back(r[0][2].c_str());
//   }
//   if (num_cols > 2)
//     temp.parameter.push_back(r[0][3].c_str());
//   tx.commit();
//   c.disconnect();
//   return temp;
// }

int readLogId()
{
  pqxx::connection c(DBCONNECTINFO);
  std::string sql("SELECT MAX(id) FROM LogTable;");
  pqxx::result r;
  pqxx::work tx(c);
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return -1;
  }

  const pqxx::field field = r[0][0];
  std::string str = field.c_str();
  if (str == "")
  {
    c.disconnect();
    return -1;
  }
  int maxId = std::stoi(str);
  tx.commit();
  c.disconnect();
  return maxId;
}

void register_new_voter()
{
  std::string name, group;
  std::cout << "input new voter name:";
  std::cin >> name;
  std::cout << "input new voter group:";
  std::cin >> group;

  ClientContext remotecontext;
  voting::Status remoteVstatus, localVstatus;
  Voter newVoter;
  newVoter.set_name(name);
  newVoter.set_group(group);

  // create a key pair
  unsigned char pk[crypto_sign_PUBLICKEYBYTES];
  unsigned char sk[crypto_sign_SECRETKEYBYTES];
  crypto_sign_keypair(pk, sk);

  newVoter.set_public_key(pk, crypto_sign_PUBLICKEYBYTES);

  localVstatus = localRegisterVoter(newVoter);
  if (localVstatus.code() == 0)
  {
    // send grpc request to remote server, rpc RegisterVoter(Voter) returns (Status);
    if (isSync == 0)
    {
      std::cout << "send remote RegisterVoter" << std::endl;
      Status remoteStatus;
      remoteStatus = GRPCconnect->remoteStub_->RegisterVoter(&remotecontext, newVoter, &remoteVstatus);
      std::cout << "return remote RegisterVoter" << std::endl;
    }
    // write to log
    struct log temp;
    temp.id = ++logid;
    temp.target = "RegisterVoter";
    temp.parameter.push_back(name);
    loglist.push_back(temp);
    writeLog(temp);

    // output the key pair
    std::cout << "Successful registration\n";
    std::fstream file;
    std::string filename;
    filename = "key/" + name + "pk";
    std::cout << "public key save in " << filename << std::endl;
    file.open(filename, std::fstream::out | std::fstream::binary);
    file.write((char *)pk, crypto_sign_PUBLICKEYBYTES);
    file.close();

    filename = "key/" + name + "sk";
    std::cout << "secret key save in " << filename << std::endl;
    file.open(filename, std::fstream::out | std::fstream::binary);
    file.write((char *)sk, crypto_sign_SECRETKEYBYTES);
    file.close();
  }
  else if (localVstatus.code() == 1)
    std::cout << "Voter with the same name already exists\n";
  else
    std::cout << "Undefined error\n";
  return;
}

void unregister_voter()
{
  std::string name;
  std::cout << "input unregister voter name:";
  std::cin >> name;

  VoterName vname;
  vname.set_name(name);
  voting::Status localVstatus;
  localVstatus = localUnregisterVoter(vname);
  if (localVstatus.code() == 0)
  {
    std::cout << "Successful unregistration\n";
    // send grpc request to remote server
    if (isSync == 0)
    {
      ClientContext remotecontext;
      voting::Status remoteVstatus;
      Status remoteStatus = GRPCconnect->remoteStub_->UnregisterVoter(&remotecontext, vname, &remoteVstatus);
    }
    // write to log
    struct log temp;
    temp.id = ++logid;
    temp.target = "UnregisterVoter";
    temp.parameter.push_back(name);
    writeLog(temp);
  }
  else
  {
    if (localVstatus.code() == 1)
      std::cout << "No voter with the name exists on the server\n";
    else
      std::cout << "Undefined error\n";
  }
}

int printVoter()
{
  pqxx::connection c(DBCONNECTINFO);
  std::string sql("SELECT name, groups FROM Voter;");
  pqxx::result r;
  pqxx::work tx(c);
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 1;
  }

  const int num_rows = r.size();
  if (num_rows == 0)
    std::cout << "query empty" << std::endl;
  for (auto const &row : r)
  {
    for (auto const &field : row)
      std::cout << field.c_str() << '\t';
    std::cout << std::endl;
  }

  tx.commit();
  c.disconnect();
  return 0;
}

int printElection()
{
  pqxx::connection c(DBCONNECTINFO);
  std::string sql("SELECT name, groups, end_date FROM Election;");
  pqxx::result r;
  pqxx::work tx(c);
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 1;
  }

  const int num_rows = r.size();
  if (num_rows == 0)
    std::cout << "query empty" << std::endl;
  for (auto const &row : r)
  {
    for (auto const &field : row)
      std::cout << field.c_str() << '\t';
    std::cout << std::endl;
  }

  tx.commit();
  c.disconnect();
  return 0;
}

int printVote(std::string election)
{
  pqxx::connection c(DBCONNECTINFO);
  std::string sql("SELECT * FROM " + election + ";");
  pqxx::result r;
  pqxx::work tx(c);
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 1;
  }

  const int num_rows = r.size();
  if (num_rows == 0)
    std::cout << "query empty" << std::endl;
  for (auto const &row : r)
  {
    for (auto const &field : row)
      std::cout << field.c_str() << '\t';
    std::cout << std::endl;
  }

  tx.commit();
  c.disconnect();
  return 0;
}

int printLog()
{
  pqxx::connection c(DBCONNECTINFO);
  std::string sql("SELECT * FROM LogTable;");
  pqxx::result r;
  pqxx::work tx(c);
  try
  {
    r = tx.exec(sql);
  }
  catch (const std::exception &e)
  {
    c.disconnect();
    std::cerr << e.what() << '\n';
    return 1;
  }

  const int num_rows = r.size();
  if (num_rows == 0)
    std::cout << "query empty" << std::endl;
  for (auto const &row : r)
  {
    for (auto const &field : row)
      std::cout << field.c_str() << '\t';
    std::cout << std::endl;
  }

  tx.commit();
  c.disconnect();
  return 0;
}

void controller()
{
  pthread_t serverThread;
  pthread_barrier_init(&barrier, NULL, 2);
  pthread_create(&serverThread, NULL, RunServer, NULL);
  pthread_barrier_wait(&barrier);
  pthread_barrier_destroy(&barrier);
  std::string str;
  std::string instruction("\t 'r' to register voter\n\t 'u' to unregister voter\n\t 'help' or 'h' to show the instruction\n\t 'exit' to close server\n");
  std::cout << "There is the backup server controller, please input the instruction to control the voting server\n"
            << instruction;
  // std::cout << "wait...\n";
  while (std::cin >> str)
  {
    if (str == "exit")
      break;
    else if (str == "r")
      register_new_voter();
    else if (str == "u")
      unregister_voter();
    else if (str == "voter")
      printVoter();
    else if (str == "election")
      printElection();
    else if (str == "pv")
    {
      std::string t;
      std::cin >> t;
      printVote(t);
    }
    else if (str == "log")
      printLog();
    else if (str == "help" || str == "h")
      std::cout << instruction;
    else
      std::cout << "invalid command\n";
    std::cout << "% ";
  }
  int res = pthread_cancel(serverThread);
  // void *mess;
  if (res != 0)
  {
    std::cout << "fail to end thread" << std::endl;
    return;
  }
  exit(0);
  // res = pthread_join(serverThread, &mess);
  // if (res != 0)
  // {
  //   std::cout << "waiting to end thread fail" << std::endl;
  //   return;
  // }
  // if (mess == PTHREAD_CANCELED)
  // {
  //   std::cout << "end thread" << std::endl;
  // }
  // else
  // {
  //   std::cout << "end thread error" << std::endl;
  // }

  // close child process
  // int status;
  // if (kill(pid, SIGTERM) == 0)
  //   std::cout << "server close\n";
  // else
  //   kill(pid, SIGKILL);
  // waitpid(pid, &status, 0);
  return;
}

int main(int argc, char **argv)
{
  // server port
  remote_address = "0.0.0.0:";
  local_address = "0.0.0.0:";
  if (argc == 2)
  {
    local_address += argv[1];
  }
  else if (argc == 3)
  {
    local_address += argv[1];
    remote_address += argv[2];
  }
  else
  {
    remote_address = "0.0.0.0:50051";
    local_address = "0.0.0.0:50052";
  }

  // key
  if (sodium_init() < 0)
  {
    std::cerr << "initializes libsodium error\n";
    return 1;
  }

  // try connect to Database
  try
  {
    dbConnect = new pqxx::connection(DBCONNECTINFO);
    if (dbConnect->is_open())
    {
      std::cout << "Opened database successfully: " << dbConnect->dbname() << std::endl;
    }
  }
  catch (const std::exception &e)
  {
    std::cerr << e.what() << '\n';
    std::cerr << "Can't open database" << std::endl;
    return 1;
  }
  dbConnect->disconnect();
  delete dbConnect;

  // read local log
  logid = readLogId();
  isSync = 0;
  controller();
  // pid_t pid = fork();
  // // child process run rpc server
  // if (pid == 0)
  //   RunServer(local_address);
  // else // parent process
  //   controller(pid);
  return 0;
}
