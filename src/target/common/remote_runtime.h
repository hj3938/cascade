// Copyright 2017-2018 VMware, Inc.
// SPDX-License-Identifier: BSD-2-Clause
//
// The BSD-2 license (the License) set forth below applies to all parts of the
// Cascade project.  You may not use this file except in compliance with the
// License.
//
// BSD-2 License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS AS IS AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef CASCADE_SRC_TARGET_COMMON_REMOTE_RUNTIME_H
#define CASCADE_SRC_TARGET_COMMON_REMOTE_RUNTIME_H

#include <string>
#include "src/base/bits/bits.h"
#include "src/base/stream/bufstream.h"
#include "src/base/thread/asynchronous.h"

namespace cascade {

class Compiler;
class Connection;
class Engine;
class ServerEngine;

class RemoteRuntime : public Asynchronous {
  public:
    RemoteRuntime();
    ~RemoteRuntime() override;

    RemoteRuntime& set_compiler(Compiler* c);
    RemoteRuntime& set_path(const std::string& p);
    RemoteRuntime& set_port(uint32_t p);

  private:
    Compiler* compiler_;
    std::string path_;
    uint32_t port_;

    Bits bits_;
    bufstream in_buf_;
    bufstream out_buf_;

    void run_logic() override;

    // Compiler Interface:
    Engine* compile(Connection* conn);
    
    // Core Interface:
    void get_state(Connection* conn, Engine* e);
    void set_state(Connection* conn, Engine* e);
    void get_input(Connection* conn, Engine* e);
    void set_input(Connection* conn, Engine* e);
    void resync(Connection* conn, Engine* e);

    void overrides_done_step(Connection* conn, Engine* e);
    void done_step(Connection* conn, Engine* e);
    void overrides_done_simulation(Connection* conn, Engine* e);
    void done_simulation(Connection* conn, Engine* e);

    void evaluate(Connection* conn, Engine* e);
    void there_are_updates(Connection* conn, Engine* e);
    void update(Connection* conn, Engine* e);
    void there_were_tasks(Connection* conn, Engine* e);

    void conditional_update(Connection* conn, Engine* e);
    void open_loop(Connection* conn, Engine* e);
};

} // namespace cascade

#endif
