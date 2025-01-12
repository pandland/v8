// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/string-escape-analysis-reducer.h"

namespace v8::internal::compiler::turboshaft {

void StringEscapeAnalyzer::Run() {
  for (uint32_t processed = graph_.block_count(); processed > 0; --processed) {
    BlockIndex block_index = static_cast<BlockIndex>(processed - 1);

    const Block& block = graph_.Get(block_index);
    ProcessBlock(block);
  }

  // Because of loop phis, some StringConcat could now be escaping even though
  // they weren't escaping on first use.
  ReprocessStringConcats();
}

void StringEscapeAnalyzer::ProcessBlock(const Block& block) {
  for (OpIndex index : base::Reversed(graph_.OperationIndices(block))) {
    const Operation& op = graph_.Get(index);
    switch (op.opcode) {
      case Opcode::kFrameState:
        // FrameState uses are not considered as escaping.
        break;
      case Opcode::kStringConcat:
        // The inputs of a StringConcat are only escaping if the StringConcat
        // itself is already escaping itself.
        if (IsEscaping(index)) {
          MarkAllInputsAsEscaping(op);
        } else {
          maybe_non_escaping_string_concats_.push_back(V<String>::Cast(index));
        }
        break;
      case Opcode::kStringLength:
        // The first input to StringConcat is the length of the result, which
        // means that StringLength won't prevent eliding StringConcat:
        // StringLength(StringConcat(len, left, rigth)) == len
        break;
      default:
        // By default, all uses are considered as escaping their inputs.
        MarkAllInputsAsEscaping(op);
    }
  }
}

void StringEscapeAnalyzer::MarkAllInputsAsEscaping(const Operation& op) {
  for (OpIndex input : op.inputs()) {
    escaping_operations_[input] = true;
  }
}

void StringEscapeAnalyzer::RecursivelyMarkAllStringConcatInputsAsEscaping(
    const StringConcatOp* concat) {
  base::SmallVector<const StringConcatOp*, 16> to_mark;
  to_mark.push_back(concat);

  while (!to_mark.empty()) {
    const StringConcatOp* curr = to_mark.back();
    to_mark.pop_back();

    for (OpIndex input_index : curr->inputs()) {
      const Operation& input = graph_.Get(input_index);
      if (input.Is<StringConcatOp>() && !IsEscaping(input_index)) {
        escaping_operations_[input_index] = true;
        to_mark.push_back(&input.Cast<StringConcatOp>());
      }
    }
  }
}

void StringEscapeAnalyzer::ReprocessStringConcats() {
  for (V<String> index : maybe_non_escaping_string_concats_) {
    if (IsEscaping(index)) {
      RecursivelyMarkAllStringConcatInputsAsEscaping(
          &graph_.Get(index).Cast<StringConcatOp>());
    }
  }
}

}  // namespace v8::internal::compiler::turboshaft
