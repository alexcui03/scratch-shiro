#include <functional>

#include "compiler.h"
#include "scratch.h"
#include "util.h"

namespace clipcc {

#define PARAMS compiler *compiler, scratch_target *target, scratch_block *block
#define PARSE_INPUT(x) compiler->compile_input(target, block, block->inputs[x])

void compiler::init_block_map() {
    // in this function, we will initialize all the original scratch blocks for generation.
    // mention that all the command blocks should be set in block_map, and all the reporters
    // should be set in input_map.

    // motion
    this->block_map["motion_movesteps"] = [](PARAMS) -> void {
        compiler->code << "move_steps(";
        PARSE_INPUT("STEPS");
        compiler->code << ");" ENDL;
    };
    this->block_map["motion_gotoxy"] = [](PARAMS) -> void {
        compiler->code << "this->x = (";
        PARSE_INPUT("X");
        compiler->code << ");" ENDL "this->y = (";
        PARSE_INPUT("Y");
        compiler->code << ");" ENDL;
    };
    this->block_map["motion_goto"] = [](PARAMS) -> void {
        compiler->code << "{auto target = getTarget(";
        PARSE_INPUT("TO");
        compiler->code << ");" ENDL "this->x = target.x; this->y = target.y;}";
    };

    // looks
    this->block_map["looks_say"] = [](PARAMS) -> void {
        compiler->code << "say(";
        PARSE_INPUT("MESSAGE");
        compiler->code << ");" ENDL;
    };

    // sensing
    this->block_map["sensing_askandwait"] = [](PARAMS) -> void {
        compiler->code << "co_yield runtime->ask_and_wait(";
        PARSE_INPUT("QUESTION");
        compiler->code << ");" ENDL;
    };
    this->input_map["sensing_answer"] = [](PARAMS) -> void {
        compiler->code << "runtime->answer";
    };

    // operator
    this->input_map["operator_add"] = [](PARAMS) -> void {
        compiler->code << "static_cast<double>(";
        PARSE_INPUT("NUM1");
        compiler->code << ") + static_cast<double>(";
        PARSE_INPUT("NUM2");
        compiler->code << ")";
    };
    this->input_map["operator_random"] = [](PARAMS) -> void {
        compiler->code << "ccvm::random((";
        PARSE_INPUT("FROM");
        compiler->code << "), (";
        PARSE_INPUT("TO");
        compiler->code << "))";
    };

    // data
    this->block_map["data_setvariableto"] = [](PARAMS) -> void {
        block->inputs["VARIABLE"]->type = value_type::var;
        compiler->compile_input(target, block, block->inputs["VARIABLE"]);
        compiler->code << " = ";
        compiler->compile_input(target, block, block->inputs["VALUE"]);
        compiler->code << ";" ENDL;
    };
}

}
