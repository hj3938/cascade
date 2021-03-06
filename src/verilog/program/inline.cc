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

#include "src/verilog/program/inline.h"

#include <cassert>
#include "src/verilog/analyze/evaluate.h"
#include "src/verilog/analyze/navigate.h"
#include "src/verilog/analyze/resolve.h"
#include "src/verilog/ast/ast.h"
#include "src/verilog/program/elaborate.h"

namespace cascade {

Inline::Inline() : Editor() { }

void Inline::inline_source(ModuleDeclaration* md) {
  inline_ = true;
  info_ = new ModuleInfo(md);
  md->accept(this);
  info_->invalidate();
  delete info_;
}

void Inline::outline_source(ModuleDeclaration* md) {
  inline_ = false;
  md->accept(this);
  ModuleInfo(md).invalidate();
}

bool Inline::can_inline(const ModuleDeclaration* md) const {
  const auto std = md->get_attrs()->get<String>("__std");
  return std != nullptr && std->eq("logic");
}

bool Inline::is_inlined(const ModuleInstantiation* mi) {
  return mi->inline_ != nullptr;
}

const IfGenerateConstruct* Inline::get_source(const ModuleInstantiation* mi) {
  assert(mi->inline_ != nullptr);
  return mi->inline_; 
}

Inline::Qualify::Qualify() : Builder() { }

Expression* Inline::Qualify::qualify_exp(const Expression* e) {
  return e->accept(this);
}

Identifier* Inline::Qualify::qualify_id(const Identifier* id) {
  return id->accept(this);
}

Expression* Inline::Qualify::build(const Identifier* id) {
  const auto r = Resolve().get_resolution(id);
  assert(r != nullptr);
  return new Identifier(
    Resolve().get_full_id(r)->get_ids()->clone(),
    id->get_dim()->accept(this)
  );
}

void Inline::edit(CaseGenerateConstruct* cgc) {
  assert(Elaborate().is_elaborated(cgc));
  Elaborate().elaborate(cgc)->accept(this);
}

void Inline::edit(IfGenerateConstruct* igc) {
  assert(Elaborate().is_elaborated(igc));
  Elaborate().elaborate(igc)->accept(this);
}

void Inline::edit(LoopGenerateConstruct* lgc) {
  assert(Elaborate().is_elaborated(lgc));
  Elaborate().elaborate(lgc)->accept(this);
}

void Inline::edit(ModuleInstantiation* mi) {
  if (inline_) {
    inline_source(mi);
  } else {
    outline_source(mi);
  }
  // Don't descend! We only want to inline THIS module!
}

void Inline::inline_source(ModuleInstantiation* mi) {
  // Nothing to do for code which has already been inlined
  if (is_inlined(mi)) {
    return;
  }
  // Nothing to do for code that we don't support inlining
  auto src = Elaborate().elaborate(mi);
  assert(src != nullptr);
  if (!can_inline(src)) {
    return;
  }

  // Generate connections for this instantiation. We need to do this now before
  // we start modifying the instantiation. Calling ModuleInfo later might
  // result in a refresh against undefined state.
  assert(info_->connections().find(mi->get_iid()) != info_->connections().end());
  auto conns = new Many<ModuleItem>();
  for (auto& c : info_->connections().find(mi->get_iid())->second) {
    auto lhs = ModuleInfo(src).is_input(c.first) ? 
      Qualify().qualify_id(c.first) : 
      Qualify().qualify_id((Identifier*)c.second);
    auto rhs = ModuleInfo(src).is_input(c.first) ? 
      Qualify().qualify_exp(c.second) : 
      Qualify().qualify_exp(c.first);
    conns->push_back(new ContinuousAssign(
      new Maybe<DelayControl>(),
      new VariableAssign(lhs, rhs)
    ));
  }
  // Move the contents of the instantiation into new inlined code. Downgrade
  // ports to regular declarations and parameters to localparams.
  auto inline_src = new Many<ModuleItem>();
  while (!src->get_items()->empty()) {
    auto item = src->get_items()->remove_front();
    if (auto pd = dynamic_cast<PortDeclaration*>(item)) {
      auto d = pd->get_decl()->clone();
      switch (pd->get_type()) {
        case PortDeclaration::INPUT:
          d->get_attrs()->set_or_replace("__inline", new String("input"));
          break;
        case PortDeclaration::OUTPUT:
          d->get_attrs()->set_or_replace("__inline", new String("output"));
          break;
        default:
          d->get_attrs()->set_or_replace("__inline", new String("inout"));
          break;
      }
      d->swap_id(pd->get_decl()->get_id());
      inline_src->push_back(d);
      delete pd;
    } else if (auto pd = dynamic_cast<ParameterDeclaration*>(item)) {
      auto ld = new LocalparamDeclaration(
        new Attributes(new Many<AttrSpec>()),
        pd->get_signed(),
        pd->get_dim()->clone(),
        pd->get_id()->clone(),
        pd->get_val()->clone()
      );
      ld->get_attrs()->set_or_replace("__inline", new String("parameter"));
      ld->swap_id(pd->get_id());
      inline_src->push_back(ld);
      delete pd;
    } else {
      inline_src->push_back(item);
    }
  }
  // Record the number of items in the new source
  auto attrs = new Attributes(new Many<AttrSpec>());
  attrs->get_as()->push_back(new AttrSpec(
    new Identifier("__inline"),
    new Maybe<Expression>(new Number(Bits(32, (uint32_t)inline_src->size())))
  ));
  // Finally, append the connections to the new source
  inline_src->concat(conns);

  // Update inline decorations
  auto igc = new IfGenerateConstruct(
    attrs,
    new IfGenerateClause(
      new Number(Bits(true)),
      new Maybe<GenerateBlock>(new GenerateBlock(
        new Maybe<Identifier>(mi->get_iid()->clone()),
        true,
        inline_src
      ))
    ),
    new Maybe<GenerateBlock>()
  );
  mi->inline_ = igc;
  igc->parent_ = mi;
  mi->inline_->gen_ = mi->inline_->get_clauses()->front()->get_then();

  // This module is now in an inconsistent state. Invalidate its module info
  // and invalidate the scope that contains the newly inlined code.
  ModuleInfo(src).invalidate();
  Navigate(mi).invalidate();
}

void Inline::outline_source(ModuleInstantiation* mi) {
  // Nothing to do for code which has already been outlined
  if (!is_inlined(mi)) {
    return;
  }
  // Make sure that somehow we aren't outlining something we shouldn't have inlined
  assert(Elaborate().is_elaborated(mi));
  auto src = Elaborate().get_elaboration(mi);
  assert(can_inline(src));

  // Move this inlined code back into the instantiation. Replace port and
  // parameter delcarations, and delete the connections.
  const auto length = Evaluate().get_value(mi->inline_->get_attrs()->get<Number>("__inline")).to_int();
  for (size_t i = 0; i < length; ++i) {
    auto item = mi->inline_->get_clauses()->front()->get_then()->get()->get_items()->remove_front();
    if (auto ld = dynamic_cast<LocalparamDeclaration*>(item)) {
      if (ld->get_attrs()->get<String>("__inline") != nullptr) {
        auto pd = new ParameterDeclaration(
          new Attributes(new Many<AttrSpec>()),
          ld->get_signed(),
          ld->get_dim()->clone(),
          ld->get_id()->clone(),
          ld->get_val()->clone()
        );
        pd->swap_id(ld->get_id());
        src->get_items()->push_back(pd);
        delete ld;
        continue;
      } 
    } else if (auto d = dynamic_cast<Declaration*>(item)) {
      auto annot = d->get_attrs()->get<String>("__inline");
      if (annot != nullptr && !annot->eq("parameter")) {
        auto pd = new PortDeclaration(
          new Attributes(new Many<AttrSpec>()),
          annot->eq("input") ? PortDeclaration::INPUT : annot->eq("output") ? PortDeclaration::OUTPUT : PortDeclaration::INOUT,
          d->clone()
        );
        pd->get_decl()->replace_attrs(new Attributes(new Many<AttrSpec>()));
        pd->get_decl()->swap_id(d->get_id());
        src->get_items()->push_back(pd);
        delete d;
        continue;
      }
    } 
    src->get_items()->push_back(item);
  }
  // Release any references to the remainder of the code. These are the assign
  // statements that we introduced in place of the instantiation. 
  for (auto i : *mi->inline_->get_clauses()->front()->get_then()->get()->get_items()) {
    Resolve().invalidate(i);
  }

  // Remove what's left of the inlined code and revert decorations
  delete mi->inline_;
  mi->inline_ = nullptr;

  // This module is now back in a consistent state. Invalidate its module info
  // and invalidate the scope that used to contain the inlined code.
  ModuleInfo(src).invalidate();
  Navigate(mi).invalidate();
}

} // namespace cascade
