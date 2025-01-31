/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <thrift/thrift-config.h>
#include <cstring>

#include <thrift/transport/TPipe.h>
#include <thrift/transport/TPipeServer.h>
#include <thrift/TNonCopyable.h>

#ifdef _WIN32
#include <thrift/windows/OverlappedSubmissionThread.h>
#include <thrift/windows/Sync.h>
#include <accctrl.h>
#include <aclapi.h>
#include <sddl.h>
#endif //_WIN32

namespace apache {
namespace thrift {
namespace transport {

#ifdef _WIN32

using std::shared_ptr;

class TPipeServerImpl : apache::thrift::TNonCopyable {
public:
  TPipeServerImpl() {}
  virtual ~TPipeServerImpl() {}
  virtual void interrupt() = 0;
  virtual std::shared_ptr<TTransport> acceptImpl() = 0;

  virtual HANDLE getPipeHandle() = 0;
  virtual HANDLE getWrtPipeHandle() = 0;
  virtual HANDLE getClientRdPipeHandle() = 0;
  virtual HANDLE getClientWrtPipeHandle() = 0;
  virtual HANDLE getNativeWaitHandle() { return nullptr; }
};

class TAnonPipeServer : public TPipeServerImpl {
public:
  TAnonPipeServer() {
    // The anonymous pipe needs to be created first so that the server can
    // pass the handles on to the client before the serve (acceptImpl)
    // blocking call.
    if (!createAnonPipe()) {
      GlobalOutput.perror("TPipeServer Create(Anon)Pipe failed, GLE=", GetLastError());
      throw TTransportException(TTransportException::NOT_OPEN,
                                " TPipeServer Create(Anon)Pipe failed");
    }
  }

  virtual ~TAnonPipeServer() {
    PipeR_.reset();
    PipeW_.reset();
    ClientAnonRead_.reset();
    ClientAnonWrite_.reset();
  }

  virtual void interrupt() {} // not currently implemented

  virtual std::shared_ptr<TTransport> acceptImpl();

  virtual HANDLE getPipeHandle() { return PipeR_.h; }
  virtual HANDLE getWrtPipeHandle() { return PipeW_.h; }
  virtual HANDLE getClientRdPipeHandle() { return ClientAnonRead_.h; }
  virtual HANDLE getClientWrtPipeHandle() { return ClientAnonWrite_.h; }

private:
  bool createAnonPipe();

  TAutoHandle PipeR_; // Anonymous Pipe (R)
  TAutoHandle PipeW_; // Anonymous Pipe (W)

  // Client side anonymous pipe handles
  //? Do we need duplicates to send to client?
  TAutoHandle ClientAnonRead_;
  TAutoHandle ClientAnonWrite_;
};

class TNamedPipeServer : public TPipeServerImpl {
public:
  TNamedPipeServer(const std::string& pipename,
                   uint32_t bufsize,
                   uint32_t maxconnections,
                   const std::string& securityDescriptor)
    : stopping_(false),
      pipename_(pipename),
      bufsize_(bufsize),
      maxconns_(maxconnections),
      securityDescriptor_(securityDescriptor) {
    connectOverlap_.action = TOverlappedWorkItem::CONNECT;
    cancelOverlap_.action = TOverlappedWorkItem::CANCELIO;
    TAutoCrit lock(pipe_protect_);
    initiateNamedConnect(lock);
  }
  virtual ~TNamedPipeServer() {}

  virtual void interrupt() {
    TAutoCrit lock(pipe_protect_);
    cached_client_.reset();
    if (Pipe_.h != INVALID_HANDLE_VALUE) {
      stopping_ = true;
      cancelOverlap_.h = Pipe_.h;
      // This should wake up GetOverlappedResult
      thread_->addWorkItem(&cancelOverlap_);
    }
  }

  virtual std::shared_ptr<TTransport> acceptImpl();

  virtual HANDLE getPipeHandle() { return Pipe_.h; }
  virtual HANDLE getWrtPipeHandle() { return INVALID_HANDLE_VALUE; }
  virtual HANDLE getClientRdPipeHandle() { return INVALID_HANDLE_VALUE; }
  virtual HANDLE getClientWrtPipeHandle() { return INVALID_HANDLE_VALUE; }
  virtual HANDLE getNativeWaitHandle() { return listen_event_.h; }

private:
  bool createNamedPipe(const TAutoCrit &lockProof);
  void initiateNamedConnect(const TAutoCrit &lockProof);

  TAutoOverlapThread thread_;
  TOverlappedWorkItem connectOverlap_;
  TOverlappedWorkItem cancelOverlap_;

  bool stopping_;
  std::string pipename_;
  std::string securityDescriptor_;
  uint32_t bufsize_;
  uint32_t maxconns_;
  TManualResetEvent listen_event_;

  TCriticalSection pipe_protect_;
  // only read or write these variables underneath a locked pipe_protect_
  std::shared_ptr<TPipe> cached_client_;
  TAutoHandle Pipe_;
};

HANDLE TPipeServer::getNativeWaitHandle() {
  if (impl_)
    return impl_->getNativeWaitHandle();
  return nullptr;
}

//---- Constructors ----
TPipeServer::TPipeServer(const std::string& pipename, uint32_t bufsize)
  : bufsize_(bufsize), isAnonymous_(false) {
  setMaxConnections(TPIPE_SERVER_MAX_CONNS_DEFAULT);
  setPipename(pipename);
  setSecurityDescriptor(DEFAULT_PIPE_SECURITY);
}

TPipeServer::TPipeServer(const std::string& pipename, uint32_t bufsize, uint32_t maxconnections)
  : bufsize_(bufsize), isAnonymous_(false) {
  setMaxConnections(maxconnections);
  setPipename(pipename);
  setSecurityDescriptor(DEFAULT_PIPE_SECURITY);
}

TPipeServer::TPipeServer(const std::string& pipename,
                         uint32_t bufsize,
                         uint32_t maxconnections,
                         const std::string& securityDescriptor)
  : bufsize_(bufsize), isAnonymous_(false) {
  setMaxConnections(maxconnections);
  setPipename(pipename);
  setSecurityDescriptor(securityDescriptor);
}

TPipeServer::TPipeServer(const std::string& pipename) : bufsize_(1024), isAnonymous_(false) {
  setMaxConnections(TPIPE_SERVER_MAX_CONNS_DEFAULT);
  setPipename(pipename);
  setSecurityDescriptor(DEFAULT_PIPE_SECURITY);
}

TPipeServer::TPipeServer(int bufsize) : bufsize_(bufsize), isAnonymous_(true) {
  setMaxConnections(1);
  impl_.reset(new TAnonPipeServer);
}

TPipeServer::TPipeServer() : bufsize_(1024), isAnonymous_(true) {
  setMaxConnections(1);
  impl_.reset(new TAnonPipeServer);
}

//---- Destructor ----
TPipeServer::~TPipeServer() {}

bool TPipeServer::isOpen() const {
  return (impl_->getPipeHandle() != INVALID_HANDLE_VALUE);
}

//---------------------------------------------------------
// Transport callbacks
//---------------------------------------------------------
void TPipeServer::listen() {
  if (isAnonymous_)
    return;
  impl_.reset(new TNamedPipeServer(pipename_, bufsize_, maxconns_, securityDescriptor_));
}

shared_ptr<TTransport> TPipeServer::acceptImpl() {
  return impl_->acceptImpl();
}

shared_ptr<TTransport> TAnonPipeServer::acceptImpl() {
  // This 0-byte read serves merely as a blocking call.
  byte buf;
  DWORD br;
  int fSuccess = ReadFile(PipeR_.h, // pipe handle
                          &buf,     // buffer to receive reply
                          0,        // size of buffer
                          &br,      // number of bytes read
                          nullptr);    // not overlapped

  if (!fSuccess && GetLastError() != ERROR_MORE_DATA) {
    GlobalOutput.perror("TPipeServer unable to initiate pipe comms, GLE=", GetLastError());
    throw TTransportException(TTransportException::NOT_OPEN,
                              " TPipeServer unable to initiate pipe comms");
  }
  shared_ptr<TPipe> client(new TPipe(PipeR_.h, PipeW_.h));
  return client;
}

void TNamedPipeServer::initiateNamedConnect(const TAutoCrit &lockProof) {
  if (stopping_)
    return;
  if (!createNamedPipe(lockProof)) {
    GlobalOutput.perror("TPipeServer CreateNamedPipe failed, GLE=", GetLastError());
    throw TTransportException(TTransportException::NOT_OPEN, " TPipeServer CreateNamedPipe failed");
  }

  // The prior connection has been handled, so close the gate
  ResetEvent(listen_event_.h);
  connectOverlap_.reset(nullptr, 0, listen_event_.h);
  connectOverlap_.h = Pipe_.h;
  thread_->addWorkItem(&connectOverlap_);

  // Wait for the client to connect; if it succeeds, the
  // function returns a nonzero value. If the function returns
  // zero, GetLastError should return ERROR_PIPE_CONNECTED.
  if (connectOverlap_.success) {
    GlobalOutput.printf("Client connected.");
    cached_client_.reset(new TPipe(Pipe_));
    // make sure people know that a connection is ready
    SetEvent(listen_event_.h);
    return;
  }

  DWORD dwErr = connectOverlap_.last_error;
  switch (dwErr) {
  case ERROR_PIPE_CONNECTED:
    GlobalOutput.printf("Client connected.");
    cached_client_.reset(new TPipe(Pipe_));
    // make sure people know that a connection is ready
    SetEvent(listen_event_.h);
    return;
  case ERROR_IO_PENDING:
    return; // acceptImpl will do the appropriate WaitForMultipleObjects
  default:
    GlobalOutput.perror("TPipeServer ConnectNamedPipe failed, GLE=", dwErr);
    throw TTransportException(TTransportException::NOT_OPEN,
                              " TPipeServer ConnectNamedPipe failed");
  }
}

shared_ptr<TTransport> TNamedPipeServer::acceptImpl() {
  {
    TAutoCrit lock(pipe_protect_);
    if (cached_client_.get() != nullptr) {
      shared_ptr<TPipe> client;
      // zero out cached_client, since we are about to return it.
      client.swap(cached_client_);

      // kick off the next connection before returning
      initiateNamedConnect(lock);
      return client; // success!
    }
  }

  if (Pipe_.h == INVALID_HANDLE_VALUE) {
    throw TTransportException(TTransportException::NOT_OPEN,
                              "TNamedPipeServer: someone called accept on a closed pipe server");
  }

  DWORD dwDummy = 0;

  // For the most part, Pipe_ should be protected with pipe_protect_.  We can't
  // reasonably do that here though without breaking interruptability.  However,
  // this should be safe, though I'm not happy about it.  We only need to ensure
  // that no one writes / modifies Pipe_.h while we are reading it.  Well, the
  // only two things that should be modifying Pipe_ are acceptImpl, the
  // functions it calls, and the destructor.  Those things shouldn't be run
  // concurrently anyway.  So this call is 'really' just a read that may happen
  // concurrently with interrupt, and that should be fine.
  if (GetOverlappedResult(Pipe_.h, &connectOverlap_.overlap, &dwDummy, TRUE)) {
    TAutoCrit lock(pipe_protect_);
    shared_ptr<TPipe> client;
    try {
      client.reset(new TPipe(Pipe_));
    } catch (TTransportException& ttx) {
      if (ttx.getType() == TTransportException::INTERRUPTED) {
        throw;
      }

      GlobalOutput.perror("Client connection failed. TTransportExceptionType=", ttx.getType());
      // kick off the next connection before throwing
      initiateNamedConnect(lock);
      throw TTransportException(TTransportException::CLIENT_DISCONNECT, ttx.what());
    }
    GlobalOutput.printf("Client connected.");
    // kick off the next connection before returning
    initiateNamedConnect(lock);
    return client; // success!
  }
  // if we got here, then we are in an error / shutdown case
  DWORD gle = GetLastError(); // save error before doing cleanup
  GlobalOutput.perror("TPipeServer ConnectNamedPipe GLE=", gle);
  if(gle == ERROR_OPERATION_ABORTED) {
    TAutoCrit lock(pipe_protect_);    	// Needed to insure concurrent thread to be out of interrupt.
    throw TTransportException(TTransportException::INTERRUPTED, "TPipeServer: server interupted");
  }
  throw TTransportException(TTransportException::NOT_OPEN, "TPipeServer: client connection failed");
}

void TPipeServer::interrupt() {
  if (impl_)
    impl_->interrupt();
}

void TPipeServer::close() {
  impl_.reset();
}

bool TNamedPipeServer::createNamedPipe(const TAutoCrit& /*lockProof*/) {

  PSECURITY_DESCRIPTOR psd = nullptr;
  ULONG size = 0;

  if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(securityDescriptor_.c_str(),
                                                            SDDL_REVISION_1, &psd, &size)) {
    DWORD lastError = GetLastError();
    GlobalOutput.perror("TPipeServer::ConvertStringSecurityDescriptorToSecurityDescriptorA() GLE=",
                        lastError);
    throw TTransportException(
        TTransportException::NOT_OPEN,
        "TPipeServer::ConvertStringSecurityDescriptorToSecurityDescriptorA() failed", lastError);
  }

  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = psd;
  sa.bInheritHandle = FALSE;

  // Create an instance of the named pipe
  TAutoHandle hPipe(CreateNamedPipeA(pipename_.c_str(),        // pipe name
                                     PIPE_ACCESS_DUPLEX |      // read/write access
                                         FILE_FLAG_OVERLAPPED, // async mode
                                     PIPE_TYPE_BYTE |          // byte type pipe
                                         PIPE_READMODE_BYTE,   // byte read mode
                                     maxconns_,                // max. instances
                                     bufsize_,                 // output buffer size
                                     bufsize_,                 // input buffer size
                                     0,                        // client time-out
                                     &sa));                    // security attributes

  auto lastError = GetLastError();
  if (psd)
    LocalFree(psd);

  if (hPipe.h == INVALID_HANDLE_VALUE) {
    Pipe_.reset();
    GlobalOutput.perror("TPipeServer::TCreateNamedPipe() GLE=", lastError);
    throw TTransportException(TTransportException::NOT_OPEN, "TCreateNamedPipe() failed",
                              lastError);
  }

  Pipe_.reset(hPipe.release());
  return true;
}

bool TAnonPipeServer::createAnonPipe() {
  SECURITY_ATTRIBUTES sa;
  SECURITY_DESCRIPTOR sd; // security information for pipes

  if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
    GlobalOutput.perror("TPipeServer InitializeSecurityDescriptor (anon) failed, GLE=",
                        GetLastError());
    return false;
  }
  if (!SetSecurityDescriptorDacl(&sd, true, nullptr, false)) {
    GlobalOutput.perror("TPipeServer SetSecurityDescriptorDacl (anon) failed, GLE=",
                        GetLastError());
    return false;
  }
  sa.lpSecurityDescriptor = &sd;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = true; // allow passing handle to child

  HANDLE ClientAnonReadH, PipeW_H, ClientAnonWriteH, Pipe_H;
  if (!CreatePipe(&ClientAnonReadH, &PipeW_H, &sa, 0)) // create stdin pipe
  {
    GlobalOutput.perror("TPipeServer CreatePipe (anon) failed, GLE=", GetLastError());
    return false;
  }
  if (!CreatePipe(&Pipe_H, &ClientAnonWriteH, &sa, 0)) // create stdout pipe
  {
    GlobalOutput.perror("TPipeServer CreatePipe (anon) failed, GLE=", GetLastError());
    CloseHandle(ClientAnonReadH);
    CloseHandle(PipeW_H);
    return false;
  }

  ClientAnonRead_.reset(ClientAnonReadH);
  ClientAnonWrite_.reset(ClientAnonWriteH);
  PipeR_.reset(Pipe_H);
  PipeW_.reset(PipeW_H);

  return true;
}

//---------------------------------------------------------
// Accessors
//---------------------------------------------------------
std::string TPipeServer::getPipename() {
  return pipename_;
}

void TPipeServer::setPipename(const std::string& pipename) {
  if (pipename.find("\\\\") == std::string::npos)
    pipename_ = "\\\\.\\pipe\\" + pipename;
  else
    pipename_ = pipename;
}

int TPipeServer::getBufferSize() {
  return bufsize_;
}
void TPipeServer::setBufferSize(int bufsize) {
  bufsize_ = bufsize;
}

HANDLE TPipeServer::getPipeHandle() {
  return impl_ ? impl_->getPipeHandle() : INVALID_HANDLE_VALUE;
}
HANDLE TPipeServer::getWrtPipeHandle() {
  return impl_ ? impl_->getWrtPipeHandle() : INVALID_HANDLE_VALUE;
}
HANDLE TPipeServer::getClientRdPipeHandle() {
  return impl_ ? impl_->getClientRdPipeHandle() : INVALID_HANDLE_VALUE;
}
HANDLE TPipeServer::getClientWrtPipeHandle() {
  return impl_ ? impl_->getClientWrtPipeHandle() : INVALID_HANDLE_VALUE;
}

bool TPipeServer::getAnonymous() {
  return isAnonymous_;
}
void TPipeServer::setAnonymous(bool anon) {
  isAnonymous_ = anon;
}

void TPipeServer::setSecurityDescriptor(const std::string& securityDescriptor) {
  securityDescriptor_ = securityDescriptor;
}

void TPipeServer::setMaxConnections(uint32_t maxconnections) {
  if (maxconnections == 0)
    maxconns_ = 1;
  else if (maxconnections > PIPE_UNLIMITED_INSTANCES)
    maxconns_ = PIPE_UNLIMITED_INSTANCES;
  else
    maxconns_ = maxconnections;
}

#endif //_WIN32
}
}
} // apache::thrift::transport
