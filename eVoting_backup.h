#ifndef EVOTING_BACKUP_H
#define EVOTING_BACKUP_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdlib.h>
#include <vector>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
// #include <sys/types.h>
// #include <sys/wait.h>
// #include <signal.h>
#include <sodium.h>
#include <pqxx/pqxx>

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "eVoting.grpc.pb.h"

using google::protobuf::Timestamp;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
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
using voting::ServerVote;
using voting::User;
using voting::Vote;
using voting::VoteCount;
using voting::Voter;
using voting::VoterName;

struct log
{
    int id;                             // log id
    std::string target;                 // rpc name
    std::vector<std::string> parameter; // rpc parameter
    // int status;      // 0x00 for create, 0x01 complete local, 0x10 complete sync, 0x11 both complete
};

struct authStruct
{
    std::string name;
    std::string challenge;
    std::string token;
    std::time_t startTime;
};

class ServerConnection
{
public:
    // connect to both server
    ServerConnection();
    ServerConnection(std::shared_ptr<Channel> channel);
    std::shared_ptr<Channel> remoteChannel;
    std::unique_ptr<eVoting::Stub> remoteStub_;
};

class eVotingServer final : public eVoting::Service
{
public:
    explicit eVotingServer();
    Status PreAuth(ServerContext *context, const VoterName *voterName, Challenge *challenge);
    Status Auth(ServerContext *context, const AuthRequest *authRequest, AuthToken *authToken);
    Status CreateElection(ServerContext *context, const Election *election, voting::Status *status);
    Status CastVote(ServerContext *context, const Vote *vote, voting::Status *status);
    Status GetResult(ServerContext *context, const ElectionName *electionName, ElectionResult *electionResult);
    Status RegisterVoter(ServerContext *context, const Voter *voter, voting::Status *status);
    Status UnregisterVoter(ServerContext *context, const VoterName *voterName, voting::Status *status);
    Status SyncDb(ServerContext *context, const voting::Status *status, voting::Status *restatus);
    Status SyncUser(ServerContext *context, const voting::Status *status, voting::User *User);
    Status SyncVote(ServerContext *context, const voting::ServerVote *serverVote, voting::Status *status);
    Status SyncAuth(ServerContext *context, const voting::User *user, voting::Status *status);
    // run log send by remote server, and sync auth status
    void synServer();

private:
    int searchAuthByName(std::string _name);
    bool isAuth(std::string _token);

    std::vector<authStruct> userlist;
    // ServerConnection GRPCconnect;
};

voting::Status localRegisterVoter(Voter _voter);
voting::Status localUnregisterVoter(VoterName _voterName);

// voter exist return 1, not exist return 0, error return 2
int checkVoterExist(std::string name);

// Election exist return 1, not exist return 0, error return 2
int checkElectionExist(std::string name);

void *RunServer(void *);

void unregister_voter();

void register_new_voter();

// parent process run server controler
void controller();

// debug function
int printVoter();
int printElection();
int printVote(std::string election);
int printLog();

// search DB data from name, use RPC to sync data
void synRegisterVoter(std::string name);

void synUnregisterVoter(std::string name);

void synCreateElection(std::string name);

void synCastVote(std::string name, std::string election);

// write the log to database
void writeLog(const struct log &data);

// return the log value of log id
// struct log readLog(int id);

// return the lastest value of log id
int readLogId();

#endif