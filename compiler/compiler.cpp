#include "compiler.h"

#include <string>
#include <iostream>
#include <vector>
#include <algorithm>
#include <iterator>
#include <iomanip>
#include <json/json.h>

#include "scratch.h"
#include "util.h"

namespace clipcc {

scratch_project *parse_project(const Json::Value &root);
scratch_target *parse_target(const Json::Value &root);
void parse_block(const Json::Value &root, scratch_target *target);
void parse_input_shadow(const Json::Value &root, scratch_input *input);

compiler::compiler(): project(nullptr) {
    this->init_block_map();
}

compiler::~compiler() {}

std::string compiler::compile_project(const std::string &src) {
    Json::Reader reader;
    Json::Value root;
    bool success = reader.parse(src, root);
    if (!success) {
        std::cerr << "error: cannot parse json" << std::endl;
    }
    this->project = parse_project(root);
    this->code = "// This is generated by clipcc-compiler.\n"
        "#include <vm/clipcc_vm.h>\n\n";
    this->var_map.clear();
    this->target_name.clear();
    this->compile();
    return this->code;
}

void compiler::compile() {
    std::string main_code = "ccvm::runtime runtime;" ENDL;
    for (int i = 0; auto target : this->project->targets) {
        this->target_name.push_back(target->name);
        std::string class_name = "target_" + std::to_string(i);
        std::string parent_class = target->is_stage ? "stage" : "sprite";
        this->code << "class " << class_name << ": public ccvm::"
            << parent_class << " {" ENDL << "public:" ENDL;
        // generate variables
        for (int j = 0; auto var : target->variables) {
            this->code << "ccvm::variant var_" << std::to_string(j) << ";" ENDL;
            this->var_map[target][var->name] = j;
            ++j;
        }
        
        // constructor
        this->code << class_name << "(ccvm::runtime *rt): ccvm::" << parent_class << "(rt) {" ENDL;
        this->code << "}" ENDL;
        
        // destructor
        this->code << "~" << class_name << "() {" ENDL;
        this->code << "}" ENDL;

        // init target
        main_code << class_name << " " << class_name << "(&runtime);" ENDL;

        // generate script
        for (int j = 0; auto block : target->blocks) {
            if (!block->top_level) continue;

            std::string func_name = "func_" + std::to_string(j);
            this->code << "ccvm::coroutine " << func_name << "() {" ENDL;
            // maybe we should move below codes to an event_map for generating codes.
            // @todo
            if (block->opcode == "event_whenflagclicked") {
                main_code << "runtime.push_thread(new ccvm::thread(std::bind(&"
                    << class_name << "::" << func_name << ", &" << class_name << ")));" ENDL;
            }
            else if (block->opcode == "event_whenbroadcastreceived") {
                main_code << "runtime.push_broadcast(\"" << block->inputs["BROADCAST_OPTION"]->value
                    << "\", std::bind(&" << class_name << "::" << func_name << ", &" << class_name << "));" ENDL;
            }
            else {
                std::cerr << "[warn] unsupported opcode of top level block: " << block->opcode << std::endl;
            }

            auto cur_block = block;
            while (!cur_block->next.empty()) {
                if (!target->blocks_map.contains(cur_block->next)) {
                    std::cerr << "[error] cannot found block id " << cur_block->next << std::endl;
                    break;
                }
                cur_block = target->blocks_map[cur_block->next];
                if (this->block_map.contains(cur_block->opcode)) {
                    this->block_map[cur_block->opcode](this, target, cur_block);
                }
                else {
                    std::cerr << "[warn] unsupported opcode: " << cur_block->opcode << std::endl;
                }
            }

            this->code << "}" ENDL;
            ++j;
        }

        this->code << "};" ENDL;
        ++i;
    }

    // generate main function
    this->code << "int main() {" ENDL << main_code
        << "while (!runtime.should_terminate()) runtime.excute();" ENDL "return 0;" ENDL "}" ENDL;
}

void compiler::compile_input(scratch_target *target, scratch_block *parent, scratch_input *input) {
    if (input->block_id.empty()) {
        if (input->type == value_type::var) {
            // if the input is an var
            this->code << "this->var_" << std::to_string(this->var_map[target][input->value]);
        }
        else if (input->type == value_type::string) {
            this->code << "\"" << stringify(input->value) << "\"";
        }
        else {
            // check type here
            if (is_valid_number(input->value)) this->code << input->value;
            else this->code << "\"" << stringify(input->value) << "\"";
        }
    }
    else {
        // the block id is set, which means it receives value from another block
        if (!target->blocks_map.contains(input->block_id)) {
            std::cerr << "[error] cannot found block id " << input->block_id << std::endl;
            return;
        }
        auto block = target->blocks_map[input->block_id];
        if (this->input_map.contains(block->opcode)) {
            this->input_map[block->opcode](this, target, block);
        }
        else {
            // we pass an nullptr here, only for test
            std::cerr << "[warn] unsupported opcode: " << block->opcode << std::endl;
            this->code << "nullptr";
        }
    }
}

scratch_project *parse_project(const Json::Value &root) {
    auto project = new scratch_project;
    for (auto target : root["targets"]) {
        project->targets.push_back(parse_target(target));
    }

    // parse monitors
    // @todo

    return project;
}

scratch_target *parse_target(const Json::Value &root) {
    auto target = new scratch_target{
        .name = root["name"].asString(),
        .x = root["x"].asInt(),
        .y = root["y"].asInt(),
        .direction = root["direction"].asInt(),
        .size = root["size"].asInt(),
        .current_costume = root["currentCostume"].asInt(),
        .rotation_style = root["rotationStyle"].asString(),
        .is_stage = root["isStage"].asBool(),
        .draggable = root["draggable"].asBool(),
        .volume = root["volume"].asInt(),
    };
    if (target->is_stage) {
        target->tempo = root["tempo"].asInt();
        target->video_transparency = root["videoTransparency"].asInt();
    }

    // parse blocks
    parse_block(root["blocks"], target);

    // parse variables
    for (auto var_id : root["variables"].getMemberNames()) {
        auto var_src = root["variables"][var_id];
        auto var = new scratch_variable{
            .is_cloud = var_src.size() == 3 && var_src[2].asBool() && target->is_stage,
            .id = var_id,
            .name = var_src[0].asString()
        };
        var->value = var->original = var_src[1].asString();
        target->variables.push_back(var);
    }

    // parse lists
    for (auto list_id : root["lists"].getMemberNames()) {
        auto list_src = root["lists"][list_id];
        auto list = new scratch_list{
            .is_cloud = list_src.size() == 3 && list_src[2].asBool() && target->is_stage,
            .id = list_id,
            .name = list_src[0].asString()
        };
        for (auto v : list_src[1]) {
            list->value.push_back(v.asString());
        }
        target->lists.push_back(list);
    }

    // is broadcast needed?
    // @todo

    // is comments needed?
    // @todo

    // load sounds and costumes
    // @todo

    return target;
}

void parse_block(const Json::Value &root, scratch_target *target) {
    for (auto block_id : root.getMemberNames()) {
        auto block_src = root[block_id];
        if (block_src.isArray()) {
            // this is one of the primitives, i don't know what situation
            // will trigger such block structure.
            // @todo
            std::cerr << "[warn] unsupported: block " << block_id << " is an array." << std::endl;
            continue;
        }
        auto block = new scratch_block;
        block->id = block_id;
        block->opcode = block_src["opcode"].asString();
        block->top_level = block_src["topLevel"].asBool();
        block->parent = block_src["parent"].isNull() ? "" : block_src["parent"].asString();
        block->next = block_src["next"].isNull() ? "" : block_src["next"].asString();
        // we didn't load x, y and shadow here, because they won't be used.

        // parse inputs
        for (auto inputs = block_src["inputs"]; auto input_name : inputs.getMemberNames()) {
            auto input_src = inputs[input_name];
            // if it has already been deserialized, skip it.
            // but i don't know why to do that.
            if (!input_src.isArray()) continue;
            auto input = new scratch_input;
            input->name = input_name;
            int shadow_info = input_src[0].asInt();
            // maybe we should check the json type first
            // @todo
            if (shadow_info == 1) { // INPUT_SAME_BLOCK_SHADOW
                if (input_src[1].isArray()) parse_input_shadow(input_src[1], input);
                else input->block_id = input_src[1].asString();
            }
            else if (shadow_info == 2) { //INPUT_BLOCK_NO_SHADOW
                // this only accepts blocks
                input->block_id = input_src[1].asString();
            }
            else { // 3, INPUT_DIFF_BLOCK_SHADOW
                // block and shadow both exists
                if (input_src[1].isArray()) parse_input_shadow(input_src[1], input);
                else input->block_id = input_src[1].asString();
            }
            block->inputs[input_name] = input;
        }
        
        // parse fields
        for (auto fields = block_src["fields"]; auto field_name : fields.getMemberNames()) {
            // all fields can be treated as same as a constant string input
            auto input = new scratch_input;
            input->name = field_name;
            input->type = value_type::string;
            input->value = fields[field_name][0].asString();
            block->inputs[field_name] = input;
        }
        
        target->blocks.push_back(block);
        target->blocks_map[block->id] = block;
    }
}

void parse_input_shadow(const Json::Value &root, scratch_input *input) {
    // in this function, we only parse the original primitive type, which is
    // defined by scratch-vm, but the type system is weak, because MATH_NUM_PRIMITIVE
    // can pass a string acctually, like "last" or "all" in a block of list.
    // therefore, we need more check to confirm its actual type in runtime.
    // @todo
    switch (root[0].asInt()) {
    case 4: [[fallthrough]]; // MATH_NUM_PRIMITIVE
    case 5: [[fallthrough]]; // POSITIVE_NUM_PRIMITIVE
    case 6: // WHOLE_NUM_PRIMITIVE
        // these number should be checked whether it is double/int,
        // because some blocks like operator_random perform differently based on its type.
        // @todo
        input->type = value_type::real_number;
        break;
    case 7: [[fallthrough]]; // INTEGER_NUM_PRIMITIVE
    case 8: [[fallthrough]]; // ANGLE_NUM_PRIMITIVE
    case 9: // COLOR_PICKER_PRIMITIVE
        input->type = value_type::integer;
        break;
    case 10: [[fallthrough]]; // TEXT_PRIMITIVE
    case 11: // BROADCAST_PRIMITIVE
        // only broadcast's name is needed, so it can be treated as a string.
        input->type = value_type::string;
        break;
    case 12: [[fallthrough]]; // VAR_PRIMITIVE
    case 13: // LIST_PRIMITIVE
        input->type = value_type::var;
        break;
    default:
        std::cerr << "[warn] unknown primitive type: " << root[0].asInt() << std::endl;
    }
    // original string needs to be stored for checking
    input->value = root[1].asString();
}

}
