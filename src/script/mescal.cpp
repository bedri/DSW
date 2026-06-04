#include "script/mescal.h"
#include "utilstrencodings.h"
#include "uint256.h"
#include <map>
#include <vector>
#include <stdexcept>
#include <sstream>

// Helper to check if a vector is a valid pubkey (33 bytes compressed or 65 bytes uncompressed)
static bool IsPubKey(const std::vector<unsigned char>& vch) {
    return (vch.size() == 33 && (vch[0] == 0x02 || vch[0] == 0x03)) ||
           (vch.size() == 65 && vch[0] == 0x04);
}

static CScript CompileBasicBlock(const UniValue& blockObj, const std::map<std::string, UniValue>& basicBlocks, std::string& errorStr) {
    CScript script;
    std::string role = blockObj["role"].get_str();
    UniValue inputs = blockObj["inputs"];

    if (role == "hash160") {
        if (inputs.size() < 1) {
            errorStr = "hash160 requires at least 1 input";
            return CScript();
        }
        UniValue input = inputs[0];
        std::string valStr = input["value"].get_str();
        if (IsHex(valStr)) {
            script << ParseHex(valStr);
        } else {
            script << std::vector<unsigned char>(valStr.begin(), valStr.end());
        }
        script << OP_HASH160;
    } else if (role == "check-signature-verification") {
        if (inputs.size() < 1) {
            errorStr = "check-signature-verification requires at least 1 input";
            return CScript();
        }
        std::string pubkeyHex = inputs[0]["value"].get_str();
        script << ParseHex(pubkeyHex);
        script << OP_CHECKSIGVERIFY;
    } else if (role == "equalverify-checksig") {
        if (inputs.size() < 1) {
            errorStr = "equalverify-checksig requires at least 1 input";
            return CScript();
        }
        std::string pubkeyHashHex = inputs[0]["value"].get_str();
        script << OP_DUP << OP_HASH160 << ParseHex(pubkeyHashHex) << OP_EQUALVERIFY << OP_CHECKSIG;
    } else if (role == "number") {
        if (inputs.size() < 1) {
            errorStr = "number requires at least 1 input";
            return CScript();
        }
        int64_t val = inputs[0]["value"].get_int64();
        script << CScriptNum(val);
    } else if (role == "multi-signature") {
        int m = 0, n = 0;
        UniValue sigs;
        for (unsigned int i = 0; i < inputs.size(); i++) {
            std::string name = inputs[i]["name"].get_str();
            if (name == "m") m = inputs[i]["value"].get_int();
            else if (name == "n") n = inputs[i]["value"].get_int();
            else if (name == "Signatures") sigs = inputs[i]["value"];
        }
        if (m <= 0 || n <= 0 || sigs.size() == 0) {
            errorStr = "multi-signature requires valid m, n, and Signatures array";
            return CScript();
        }
        script << CScriptNum(m);
        for (unsigned int i = 0; i < sigs.size(); i++) {
            script << ParseHex(sigs[i].get_str());
        }
        script << CScriptNum(n) << OP_CHECKMULTISIG;
    } else if (role == "lock-time") {
        if (inputs.size() < 1) {
            errorStr = "lock-time requires lock-time value";
            return CScript();
        }
        int64_t lockTime = inputs[0]["value"].get_int64();
        script << CScriptNum(lockTime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP;
    } else if (role == "add") {
        script << OP_ADD;
    } else if (role == "sub") {
        script << OP_SUB;
    } else if (role == "bool-and") {
        script << OP_BOOLAND;
    } else if (role == "bool-or") {
        script << OP_BOOLOR;
    } else if (role == "num-equal") {
        script << OP_NUMEQUAL;
    } else if (role == "greater-than") {
        script << OP_GREATERTHAN;
    } else {
        errorStr = "Unknown basic block role: " + role;
    }
    return script;
}

static CScript CompileAction(const UniValue& action, const std::map<std::string, UniValue>& basicBlocks, const std::map<std::string, UniValue>& conditions, std::string& errorStr);

static CScript CompileCondition(const UniValue& condObj, const std::map<std::string, UniValue>& basicBlocks, const std::map<std::string, UniValue>& conditions, std::string& errorStr) {
    CScript script;
    UniValue exprs = condObj["expressions"];
    UniValue trueBranch = condObj["true"];
    UniValue falseBranch = condObj["false"];

    // Compile expressions
    for (unsigned int i = 0; i < exprs.size(); i++) {
        CScript exprScript = CompileAction(exprs[i], basicBlocks, conditions, errorStr);
        if (!errorStr.empty()) return CScript();
        script += exprScript;
        if (i > 0) {
            script << OP_BOOLAND; // Combine expressions with AND
        }
    }

    script << OP_IF;

    // Compile True branch
    for (unsigned int i = 0; i < trueBranch.size(); i++) {
        CScript trueScript = CompileAction(trueBranch[i], basicBlocks, conditions, errorStr);
        if (!errorStr.empty()) return CScript();
        script += trueScript;
    }

    // Compile False branch if present
    if (falseBranch.size() > 0) {
        script << OP_ELSE;
        for (unsigned int i = 0; i < falseBranch.size(); i++) {
            CScript falseScript = CompileAction(falseBranch[i], basicBlocks, conditions, errorStr);
            if (!errorStr.empty()) return CScript();
            script += falseScript;
        }
    }

    script << OP_ENDIF;
    return script;
}

static CScript CompileAction(const UniValue& action, const std::map<std::string, UniValue>& basicBlocks, const std::map<std::string, UniValue>& conditions, std::string& errorStr) {
    std::string type = action["type"].get_str();
    std::string name = action["name"].get_str();

    if (type == "basic") {
        auto it = basicBlocks.find(name);
        if (it != basicBlocks.end()) {
            return CompileBasicBlock(it->second, basicBlocks, errorStr);
        } else {
            // Check if inline definition
            if (action.exists("role")) {
                return CompileBasicBlock(action, basicBlocks, errorStr);
            }
            errorStr = "Basic block definition not found: " + name;
            return CScript();
        }
    } else if (type == "condition") {
        auto it = conditions.find(name);
        if (it != conditions.end()) {
            return CompileCondition(it->second, basicBlocks, conditions, errorStr);
        } else {
            // Check if inline definition
            if (action.exists("role") && action["role"].get_str() == "if-condition") {
                return CompileCondition(action, basicBlocks, conditions, errorStr);
            }
            errorStr = "Condition block definition not found: " + name;
            return CScript();
        }
    }
    errorStr = "Unknown action type: " + type;
    return CScript();
}

CScript CMescal::Compile(const std::string& jsonStr, std::string& errorStr) {
    UniValue root;
    if (!root.read(jsonStr)) {
        errorStr = "Failed to parse JSON string";
        return CScript();
    }

    std::map<std::string, UniValue> basicBlocks;
    std::map<std::string, UniValue> conditions;
    std::map<std::string, UniValue> contracts;

    // Load symbol tables if present
    if (root.exists("basic") && root["basic"].isObject()) {
        const std::vector<std::string>& keys = root["basic"].getKeys();
        for (const auto& k : keys) basicBlocks[k] = root["basic"][k];
    }
    if (root.exists("condition") && root["condition"].isObject()) {
        const std::vector<std::string>& keys = root["condition"].getKeys();
        for (const auto& k : keys) conditions[k] = root["condition"][k];
    }
    if (root.exists("contract") && root["contract"].isObject()) {
        const std::vector<std::string>& keys = root["contract"].getKeys();
        for (const auto& k : keys) contracts[k] = root["contract"][k];
    }

    // Determine the contract object to compile
    UniValue targetContract;
    if (root.exists("type") && root["type"].get_str() == "contract") {
        targetContract = root;
    } else {
        std::string active = "";
        if (root.exists("active_contract")) {
            active = root["active_contract"].get_str();
        } else if (!contracts.empty()) {
            active = contracts.begin()->first;
        }

        if (active.empty() || contracts.find(active) == contracts.end()) {
            errorStr = "No active contract specified or found";
            return CScript();
        }
        targetContract = contracts[active];
    }

    CScript script;
    UniValue actions = targetContract["actions"];
    if (!actions.isArray()) {
        errorStr = "Contract actions must be an array";
        return CScript();
    }

    for (unsigned int i = 0; i < actions.size(); i++) {
        CScript actionScript = CompileAction(actions[i], basicBlocks, conditions, errorStr);
        if (!errorStr.empty()) return CScript();
        script += actionScript;
    }

    return script;
}

UniValue CMescal::Decompile(const CScript& script, std::string& errorStr) {
    UniValue root(UniValue::VOBJ);
    UniValue actions(UniValue::VARR);

    CScript::const_iterator pc = script.begin();
    opcodetype opcode;
    std::vector<unsigned char> vch;

    struct DecompileState {
        std::vector<UniValue> stack;
    };

    DecompileState state;
    int basicCount = 0;
    int condCount = 0;

    // Helper to peek/match script patterns
    auto MatchPattern = [&](CScript::const_iterator temp_pc, const std::vector<opcodetype>& pattern, std::vector<std::vector<unsigned char>>& pushedData) -> bool {
        pushedData.clear();
        for (opcodetype expected : pattern) {
            opcodetype op;
            std::vector<unsigned char> data;
            if (!script.GetOp(temp_pc, op, data)) return false;
            if (expected == OP_PUSHDATA4) {
                // Wildcard for data push
                if (!(op >= 0 && op <= OP_PUSHDATA4)) return false;
                pushedData.push_back(data);
            } else if (op != expected) {
                return false;
            }
        }
        return true;
    };

    while (pc < script.end()) {
        CScript::const_iterator next_pc = pc;
        std::vector<std::vector<unsigned char>> pushes;

        // Pattern 1: lock-time (<expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP)
        if (MatchPattern(next_pc, {OP_PUSHDATA4, OP_CHECKLOCKTIMEVERIFY, OP_DROP}, pushes)) {
            pc = next_pc; // Consume matched instructions
            CScriptNum val(pushes[0], true);
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "lock-time-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "lock-time"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "timestamp-or-block-height"));
            input.push_back(Pair("name", "Lock-Until"));
            input.push_back(Pair("value", val.getint64()));
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Pattern 2: equalverify-checksig (OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG)
        next_pc = pc;
        if (MatchPattern(next_pc, {OP_DUP, OP_HASH160, OP_PUSHDATA4, OP_EQUALVERIFY, OP_CHECKSIG}, pushes)) {
            pc = next_pc;
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "equalverify-checksig-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "equalverify-checksig"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "pubkeyhash"));
            input.push_back(Pair("name", "PubkeyHash"));
            input.push_back(Pair("value", HexStr(pushes[0].begin(), pushes[0].end())));
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Pattern 3: check-signature-verification (<pubkey> OP_CHECKSIGVERIFY)
        next_pc = pc;
        if (MatchPattern(next_pc, {OP_PUSHDATA4, OP_CHECKSIGVERIFY}, pushes) && IsPubKey(pushes[0])) {
            pc = next_pc;
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "check-sig-verify-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "check-signature-verification"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "pubkey"));
            input.push_back(Pair("name", "Pubkey"));
            input.push_back(Pair("value", HexStr(pushes[0].begin(), pushes[0].end())));
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Pattern 4: hash160 (<val> OP_HASH160)
        next_pc = pc;
        if (MatchPattern(next_pc, {OP_PUSHDATA4, OP_HASH160}, pushes)) {
            pc = next_pc;
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "hash160-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "hash160"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "string-or-number"));
            input.push_back(Pair("name", "String-or-Number-to-Hash"));
            input.push_back(Pair("value", HexStr(pushes[0].begin(), pushes[0].end())));
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Parse generic opcode or push
        if (!script.GetOp(pc, opcode, vch)) {
            errorStr = "Malformed script";
            return UniValue();
        }

        if (opcode >= 0 && opcode <= OP_PUSHDATA4) {
            // Push number or raw data
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "number-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "number"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "number"));
            input.push_back(Pair("name", "Number"));
            try {
                CScriptNum num(vch, true);
                input.push_back(Pair("value", num.getint64()));
            } catch (...) {
                input.push_back(Pair("value", HexStr(vch.begin(), vch.end())));
            }
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
        } else if (opcode == OP_ADD) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "add-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "add"));
            state.stack.push_back(block);
        } else if (opcode == OP_SUB) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "sub-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "sub"));
            state.stack.push_back(block);
        } else if (opcode == OP_BOOLAND) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "bool-and-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "bool-and"));
            state.stack.push_back(block);
        } else if (opcode == OP_BOOLOR) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "bool-or-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "bool-or"));
            state.stack.push_back(block);
        } else if (opcode == OP_NUMEQUAL) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "num-equal-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "num-equal"));
            state.stack.push_back(block);
        } else if (opcode == OP_GREATERTHAN) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "greater-than-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "greater-than"));
            state.stack.push_back(block);
        } else if (opcode == OP_IF) {
            // We pop the last block as the expression
            UniValue expr(UniValue::VOBJ);
            if (!state.stack.empty()) {
                expr = state.stack.back();
                state.stack.pop_back();
            } else {
                expr.push_back(Pair("type", "basic"));
                expr.push_back(Pair("name", "true"));
                expr.push_back(Pair("role", "number"));
                UniValue inputs(UniValue::VARR);
                UniValue input(UniValue::VOBJ);
                input.push_back(Pair("value", 1));
                inputs.push_back(input);
                expr.push_back(Pair("inputs", inputs));
            }

            // Find matching ELSE or ENDIF
            CScript trueScript;
            CScript falseScript;
            bool inFalse = false;
            int ifDepth = 1;

            while (pc < script.end()) {
                opcodetype innerOp;
                std::vector<unsigned char> innerVch;
                CScript::const_iterator mark = pc;
                if (!script.GetOp(pc, innerOp, innerVch)) break;

                if (innerOp == OP_IF) {
                    ifDepth++;
                } else if (innerOp == OP_ENDIF) {
                    ifDepth--;
                    if (ifDepth == 0) break;
                } else if (innerOp == OP_ELSE && ifDepth == 1) {
                    inFalse = true;
                    continue;
                }

                // Append the raw instructions
                if (inFalse) {
                    falseScript << innerOp; // Note: simplified serialization
                } else {
                    trueScript << innerOp;
                }
            }

            UniValue condBlock(UniValue::VOBJ);
            condBlock.push_back(Pair("type", "condition"));
            condBlock.push_back(Pair("name", "if-condition-" + std::to_string(condCount++)));
            condBlock.push_back(Pair("role", "if-condition"));

            UniValue exprs(UniValue::VARR);
            exprs.push_back(expr);
            condBlock.push_back(Pair("expressions", exprs));

            std::string err;
            condBlock.push_back(Pair("true", CMescal::Decompile(trueScript, err)));
            if (falseScript.size() > 0) {
                condBlock.push_back(Pair("false", CMescal::Decompile(falseScript, err)));
            }
            state.stack.push_back(condBlock);
        }
    }

    for (const auto& item : state.stack) {
        actions.push_back(item);
    }

    root.push_back(Pair("type", "contract"));
    root.push_back(Pair("name", "Decompiled-Contract"));
    root.push_back(Pair("actions", actions));
    return root;
}
