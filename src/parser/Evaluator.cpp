#include "Evaluator.h"
#include "FunctionDef.h"
#include "ShapeRegistry.h"
#include "Debug.h"
#include "OpenDCADLexer.h"
#include "Color.h"
#include <filesystem>
#include <fstream>

namespace opendcad {

Evaluator::Evaluator()
    : env_(std::make_shared<Environment>()) {
    registerBuiltins();
}

void Evaluator::registerBuiltins() {
    builtins_["vec"] = [](const std::vector<ValuePtr>& args) -> ValuePtr {
        if (args.size() < 2 || args.size() > 3)
            throw EvalError("vec() requires 2 or 3 arguments, got " + std::to_string(args.size()));
        std::vector<double> components;
        for (const auto& a : args)
            components.push_back(a->asNumber());
        return Value::makeVector(components);
    };

    builtins_["list"] = [](const std::vector<ValuePtr>& args) -> ValuePtr {
        return Value::makeList(args);
    };

    builtins_["len"] = [](const std::vector<ValuePtr>& args) -> ValuePtr {
        if (args.size() != 1)
            throw EvalError("len() requires 1 argument");
        if (args[0]->type() == ValueType::LIST)
            return Value::makeNumber(args[0]->listLength());
        if (args[0]->type() == ValueType::VECTOR)
            return Value::makeNumber(static_cast<double>(args[0]->asVector().size()));
        if (args[0]->type() == ValueType::STRING)
            return Value::makeNumber(static_cast<double>(args[0]->asString().size()));
        throw EvalError("len() requires a list, vector, or string");
    };

    builtins_["range"] = [](const std::vector<ValuePtr>& args) -> ValuePtr {
        if (args.size() < 1 || args.size() > 3)
            throw EvalError("range() requires 1-3 arguments");
        double start = 0, stop, step = 1;
        if (args.size() == 1) {
            stop = args[0]->asNumber();
        } else {
            start = args[0]->asNumber();
            stop = args[1]->asNumber();
            if (args.size() == 3) step = args[2]->asNumber();
        }
        if (step == 0) throw EvalError("range() step cannot be 0");
        std::vector<ValuePtr> result;
        if (step > 0) {
            for (double i = start; i < stop; i += step)
                result.push_back(Value::makeNumber(i));
        } else {
            for (double i = start; i > stop; i += step)
                result.push_back(Value::makeNumber(i));
        }
        return Value::makeList(result);
    };

    builtins_["print"] = [](const std::vector<ValuePtr>& args) -> ValuePtr {
        for (size_t i = 0; i < args.size(); i++) {
            if (i > 0) std::cout << " ";
            std::cout << args[i]->toString();
        }
        std::cout << "\n";
        return Value::makeNil();
    };

    builtins_["color"] = [](const std::vector<ValuePtr>& args) -> ValuePtr {
        if (args.size() == 1 && args[0]->type() == ValueType::STRING) {
            return Value::makeColor(std::make_shared<Color>(Color::fromHex(args[0]->asString())));
        }
        if (args.size() >= 3) {
            double r = args[0]->asNumber();
            double g = args[1]->asNumber();
            double b = args[2]->asNumber();
            double a = args.size() > 3 ? args[3]->asNumber() : 1.0;
            return Value::makeColor(std::make_shared<Color>(Color::fromRGB(r, g, b, a)));
        }
        throw EvalError("color() requires (r,g,b), (r,g,b,a), or (\"#hex\")");
    };

    builtins_["material"] = [](const std::vector<ValuePtr>& args) -> ValuePtr {
        auto mat = std::make_shared<Material>();
        if (!args.empty()) {
            if (args[0]->type() == ValueType::STRING)
                mat->preset = args[0]->asString();
            else if (args[0]->type() == ValueType::COLOR)
                mat->baseColor = args[0]->asColor();
        }
        // Named args are handled at call site and passed positionally for builtins
        // For now, material("steel") just sets the preset name
        return Value::makeMaterial(mat);
    };

    // --- Phase 8: Standard Library ---

    // Named colors
    auto defColor = [this](const std::string& name, double r, double g, double b) {
        env_->defineConst(name, Value::makeColor(std::make_shared<Color>(Color::fromRGB(r, g, b))));
    };
    defColor("RED", 255, 0, 0);
    defColor("GREEN", 0, 128, 0);
    defColor("BLUE", 0, 0, 255);
    defColor("WHITE", 255, 255, 255);
    defColor("BLACK", 0, 0, 0);
    defColor("YELLOW", 255, 255, 0);
    defColor("CYAN", 0, 255, 255);
    defColor("MAGENTA", 255, 0, 255);
    defColor("ORANGE", 255, 165, 0);
    defColor("PURPLE", 128, 0, 128);
    defColor("PINK", 255, 192, 203);
    defColor("GREY", 128, 128, 128);
    defColor("GRAY", 128, 128, 128);
    defColor("DARK_RED", 139, 0, 0);
    defColor("DARK_GREEN", 0, 100, 0);
    defColor("DARK_BLUE", 0, 0, 139);
    defColor("LIGHT_GREY", 211, 211, 211);
    defColor("LIGHT_GRAY", 211, 211, 211);

    // Material presets
    auto defMat = [this](const std::string& name, const std::string& preset,
                         double metallic, double roughness) {
        auto mat = std::make_shared<Material>();
        mat->preset = preset;
        mat->metallic = metallic;
        mat->roughness = roughness;
        env_->defineConst(name, Value::makeMaterial(mat));
    };
    defMat("STEEL", "steel", 0.9, 0.3);
    defMat("ALUMINUM", "aluminum", 0.9, 0.4);
    defMat("BRASS", "brass", 0.9, 0.3);
    defMat("COPPER", "copper", 0.9, 0.2);
    defMat("CHROME", "chrome", 1.0, 0.1);
    defMat("TITANIUM", "titanium", 0.8, 0.35);
    defMat("CAST_IRON", "cast_iron", 0.7, 0.6);
    defMat("ABS_WHITE", "abs_white", 0.0, 0.7);
    defMat("ABS_BLACK", "abs_black", 0.0, 0.7);
    defMat("NYLON", "nylon", 0.0, 0.6);
    defMat("POLYCARBONATE", "polycarbonate", 0.0, 0.4);
    defMat("ACRYLIC", "acrylic", 0.0, 0.3);
    defMat("RUBBER", "rubber", 0.0, 0.95);
    defMat("WOOD", "wood", 0.0, 0.8);
    defMat("GLASS", "glass", 0.0, 0.05);
    defMat("CARBON_FIBER", "carbon_fiber", 0.3, 0.4);
    defMat("CONCRETE", "concrete", 0.0, 0.9);
}

void Evaluator::evaluate(OpenDCADParser::ProgramContext* tree,
                         const std::string& filename) {
    filename_ = filename;
    exports_.clear();
    visit(tree);
}

SourceLoc Evaluator::locFrom(antlr4::ParserRuleContext* ctx) const {
    SourceLoc loc;
    loc.filename = filename_;
    if (ctx && ctx->getStart()) {
        loc.line = ctx->getStart()->getLine();
        loc.col = ctx->getStart()->getCharPositionInLine() + 1;
    }
    return loc;
}

ValuePtr Evaluator::toValue(antlrcpp::Any result) {
    if (!result.has_value())
        return Value::makeNil();
    try {
        return std::any_cast<ValuePtr>(result);
    } catch (...) {
        return Value::makeNil();
    }
}

std::vector<ValuePtr> Evaluator::evaluateArgs(OpenDCADParser::ArgListContext* ctx) {
    std::vector<ValuePtr> args;
    if (!ctx) return args;
    for (auto* argCtx : ctx->arg()) {
        // For now, treat named args as positional (Phase 4 will handle names)
        if (auto* named = dynamic_cast<OpenDCADParser::NamedArgContext*>(argCtx)) {
            args.push_back(toValue(visit(named->expr())));
        } else if (auto* positional = dynamic_cast<OpenDCADParser::PositionalArgContext*>(argCtx)) {
            args.push_back(toValue(visit(positional->expr())));
        }
    }
    return args;
}

ValuePtr Evaluator::applyMethodChain(
    ValuePtr current,
    const std::vector<OpenDCADParser::MethodCallContext*>& methods)
{
    auto& reg = ShapeRegistry::instance();

    for (auto* method : methods) {
        std::string methodName = method->IDENT()->getText();
        auto args = evaluateArgs(method->argList());

        // Try typed methods first (works on any ValueType)
        if (reg.hasTypedMethod(current->type(), methodName)) {
            try {
                current = reg.callTypedMethod(current->type(), methodName, current, args);
            } catch (const GeometryError& e) {
                throw EvalError(std::string(e.what()), locFrom(method));
            }
        }
        // Fallback to legacy SHAPE methods
        else if (current->type() == ValueType::SHAPE && reg.hasMethod(methodName)) {
            try {
                current = reg.callMethod(methodName, current->asShape(), args);
            } catch (const GeometryError& e) {
                throw EvalError(std::string(e.what()), locFrom(method));
            }
        } else {
            throw EvalError("unknown method '" + methodName + "' on " + current->typeName(),
                            locFrom(method));
        }
    }
    return current;
}

// --- Program ---

antlrcpp::Any Evaluator::visitProgram(OpenDCADParser::ProgramContext* ctx) {
    for (auto* stmt : ctx->stmt()) {
        visit(stmt);
    }
    return Value::makeNil();
}

// --- Declarations ---

antlrcpp::Any Evaluator::visitLetStmt(OpenDCADParser::LetStmtContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr value = toValue(visit(ctx->expr()));
    env_->define(name, value);
    DEBUG_INFO("let " << name << " = " << value->toString());
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitConstStmt(OpenDCADParser::ConstStmtContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr value = toValue(visit(ctx->expr()));
    env_->defineConst(name, value);
    DEBUG_INFO("const " << name << " = " << value->toString());
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitAssignVar(OpenDCADParser::AssignVarContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr value = toValue(visit(ctx->expr()));
    try {
        env_->assign(name, value);
    } catch (const EvalError& e) {
        throw EvalError(std::string(e.what()), locFrom(ctx));
    }
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitAssignIndex(OpenDCADParser::AssignIndexContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr list;
    try {
        list = env_->lookup(name);
    } catch (const EvalError&) {
        throw EvalError("undefined variable '" + name + "'", locFrom(ctx));
    }
    if (list->type() != ValueType::LIST)
        throw EvalError("index assignment requires a list, got " + list->typeName(), locFrom(ctx));

    ValuePtr indexVal = toValue(visit(ctx->expr(0)));
    ValuePtr value = toValue(visit(ctx->expr(1)));
    int index = static_cast<int>(indexVal->asNumber());

    auto& items = const_cast<std::vector<ValuePtr>&>(list->asList());
    if (index < 0 || index >= static_cast<int>(items.size()))
        throw EvalError("list index " + std::to_string(index) + " out of bounds", locFrom(ctx));
    items[index] = value;
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitCompoundAssignStmt(OpenDCADParser::CompoundAssignStmtContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr current;
    try {
        current = env_->lookup(name);
    } catch (const EvalError&) {
        throw EvalError("undefined variable '" + name + "'", locFrom(ctx));
    }

    ValuePtr rhs = toValue(visit(ctx->expr()));
    std::string op;
    if (ctx->PLUS_EQ()) op = "+=";
    else if (ctx->MINUS_EQ()) op = "-=";
    else if (ctx->STAR_EQ()) op = "*=";
    else if (ctx->SLASH_EQ()) op = "/=";

    ValuePtr result;
    try {
        if (op == "+=") result = current->add(rhs);
        else if (op == "-=") result = current->subtract(rhs);
        else if (op == "*=") result = current->multiply(rhs);
        else if (op == "/=") result = current->divide(rhs);
        else throw EvalError("unknown compound operator", locFrom(ctx));
    } catch (const EvalError& e) {
        throw EvalError(std::string(e.what()), locFrom(ctx));
    }

    env_->assign(name, result);
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitPostfixIncrStmt(OpenDCADParser::PostfixIncrStmtContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr current;
    try {
        current = env_->lookup(name);
    } catch (const EvalError&) {
        throw EvalError("undefined variable '" + name + "'", locFrom(ctx));
    }

    double val = current->asNumber();
    if (ctx->INCR())
        env_->assign(name, Value::makeNumber(val + 1));
    else
        env_->assign(name, Value::makeNumber(val - 1));
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitExportStmt(OpenDCADParser::ExportStmtContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr value = toValue(visit(ctx->expr()));

    // When importing, exports make shapes available by name but don't produce output
    if (isImporting_) {
        env_->define(name, value);
        DEBUG_INFO("export " << name << " (suppressed during import)");
        return Value::makeNil();
    }

    ShapePtr shape;
    try {
        shape = value->asShape();
    } catch (const EvalError&) {
        throw EvalError("export '" + name + "' must be a shape, got " + value->typeName(),
                        locFrom(ctx));
    }
    exports_.push_back({name, shape});
    DEBUG_INFO("export " << name);
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitExprStmt(OpenDCADParser::ExprStmtContext* ctx) {
    visit(ctx->chainExpr());
    return Value::makeNil();
}

// --- Imports ---

antlrcpp::Any Evaluator::visitImportStmt(OpenDCADParser::ImportStmtContext* ctx) {
    std::string rawPath = ctx->STRING()->getText();
    std::string path = rawPath.substr(1, rawPath.size() - 2);  // strip quotes

    // Resolve relative to current file's directory
    std::filesystem::path resolved;
    if (std::filesystem::path(path).is_relative() && !filename_.empty()) {
        std::filesystem::path currentDir = std::filesystem::path(filename_).parent_path();
        resolved = std::filesystem::weakly_canonical(currentDir / path);
    } else {
        resolved = std::filesystem::weakly_canonical(path);
    }

    std::string resolvedStr = resolved.string();

    // Circular import detection
    if (importStack_.count(resolvedStr)) {
        throw EvalError("circular import detected: " + resolvedStr, locFrom(ctx));
    }

    // Load the file
    std::ifstream file(resolvedStr);
    if (!file) {
        throw EvalError("cannot open import file: " + resolvedStr, locFrom(ctx));
    }
    std::string src((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

    // Parse — keep parse tree alive for function body references
    auto ipt = std::make_unique<ImportedParseTree>();
    ipt->input = std::make_unique<antlr4::ANTLRInputStream>(src);
    ipt->lexer = std::make_unique<OpenDCADLexer>(ipt->input.get());
    ipt->tokens = std::make_unique<antlr4::CommonTokenStream>(ipt->lexer.get());
    ipt->parser = std::make_unique<OpenDCADParser>(ipt->tokens.get());
    auto* tree = ipt->parser->program();

    if (ipt->parser->getNumberOfSyntaxErrors() > 0) {
        throw EvalError("syntax errors in imported file: " + resolvedStr, locFrom(ctx));
    }

    importedTrees_.push_back(std::move(ipt));

    // Evaluate in current environment (brings names into scope)
    auto prevFilename = filename_;
    auto prevImporting = isImporting_;
    filename_ = resolvedStr;
    isImporting_ = true;
    importStack_.insert(resolvedStr);

    visit(tree);

    filename_ = prevFilename;
    isImporting_ = prevImporting;
    importStack_.erase(resolvedStr);

    DEBUG_INFO("import " << resolvedStr);
    return Value::makeNil();
}

// --- Control Flow ---

antlrcpp::Any Evaluator::visitBlock(OpenDCADParser::BlockContext* ctx) {
    auto prevEnv = env_;
    env_ = std::make_shared<Environment>(prevEnv);
    for (auto* stmt : ctx->stmt()) {
        visit(stmt);
    }
    env_ = prevEnv;
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitIfStmt(OpenDCADParser::IfStmtContext* ctx) {
    auto blocks = ctx->block();
    auto exprs = ctx->expr();

    for (size_t i = 0; i < exprs.size(); i++) {
        ValuePtr cond = toValue(visit(exprs[i]));
        if (cond->isTruthy()) {
            visit(blocks[i]);
            return Value::makeNil();
        }
    }

    // else block (if present — one more block than exprs)
    if (blocks.size() > exprs.size()) {
        visit(blocks.back());
    }

    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitForCStyle(OpenDCADParser::ForCStyleContext* ctx) {
    auto prevEnv = env_;
    env_ = std::make_shared<Environment>(prevEnv);

    visit(ctx->forInit());

    int iterations = 0;
    while (true) {
        ValuePtr cond = toValue(visit(ctx->expr()));
        if (!cond->isTruthy()) break;

        if (++iterations > MAX_ITERATIONS)
            throw EvalError("maximum loop iterations exceeded", locFrom(ctx));

        visit(ctx->block());
        visit(ctx->forUpdate());
    }

    env_ = prevEnv;
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitForEach(OpenDCADParser::ForEachContext* ctx) {
    std::string varName = ctx->IDENT()->getText();
    ValuePtr collection = toValue(visit(ctx->expr()));

    auto prevEnv = env_;
    env_ = std::make_shared<Environment>(prevEnv);

    if (collection->type() == ValueType::LIST) {
        const auto& items = collection->asList();
        for (const auto& item : items) {
            env_->define(varName, item);
            visit(ctx->block());
        }
    } else if (collection->type() == ValueType::VECTOR) {
        const auto& vec = collection->asVector();
        for (double v : vec) {
            env_->define(varName, Value::makeNumber(v));
            visit(ctx->block());
        }
    } else {
        throw EvalError("for-in requires a list or vector, got " + collection->typeName(),
                        locFrom(ctx));
    }

    env_ = prevEnv;
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitWhileStmt(OpenDCADParser::WhileStmtContext* ctx) {
    int iterations = 0;
    while (true) {
        ValuePtr cond = toValue(visit(ctx->expr()));
        if (!cond->isTruthy()) break;

        if (++iterations > MAX_ITERATIONS)
            throw EvalError("maximum loop iterations exceeded", locFrom(ctx));

        visit(ctx->block());
    }
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitReturnStmt(OpenDCADParser::ReturnStmtContext* ctx) {
    ValuePtr val = ctx->expr() ? toValue(visit(ctx->expr())) : Value::makeNil();
    throw ReturnValue{val};
}

// --- Functions ---

antlrcpp::Any Evaluator::visitFnDecl(OpenDCADParser::FnDeclContext* ctx) {
    auto fn = std::make_shared<FunctionDef>();
    fn->name = ctx->IDENT()->getText();
    fn->body = ctx->block();
    fn->closure = env_;

    if (ctx->paramList()) {
        for (auto* param : ctx->paramList()->param()) {
            fn->params.push_back(param->IDENT()->getText());
            if (param->expr()) {
                fn->defaults[param->IDENT()->getText()] = toValue(visit(param->expr()));
            }
        }
    }

    env_->define(fn->name, Value::makeFunction(fn));
    DEBUG_INFO("fn " << fn->name << " (" << fn->params.size() << " params)");
    return Value::makeNil();
}

ValuePtr Evaluator::callFunction(
    FunctionDefPtr fn,
    const std::vector<ValuePtr>& positionalArgs,
    const std::unordered_map<std::string, ValuePtr>& namedArgs)
{
    auto callEnv = std::make_shared<Environment>(fn->closure);

    // Bind parameters
    for (size_t i = 0; i < fn->params.size(); i++) {
        const auto& paramName = fn->params[i];
        if (i < positionalArgs.size()) {
            callEnv->define(paramName, positionalArgs[i]);
        } else if (namedArgs.count(paramName)) {
            callEnv->define(paramName, namedArgs.at(paramName));
        } else if (fn->defaults.count(paramName)) {
            callEnv->define(paramName, fn->defaults.at(paramName));
        } else {
            throw EvalError("missing argument '" + paramName + "' in call to " + fn->name);
        }
    }

    auto prevEnv = env_;
    env_ = callEnv;

    ValuePtr result = Value::makeNil();
    try {
        visit(fn->body);
    } catch (const ReturnValue& rv) {
        result = rv.value;
    }

    env_ = prevEnv;
    return result;
}

// --- For loop init/update helpers ---

antlrcpp::Any Evaluator::visitForInitLet(OpenDCADParser::ForInitLetContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr value = toValue(visit(ctx->expr()));
    env_->define(name, value);
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitForInitAssign(OpenDCADParser::ForInitAssignContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr value = toValue(visit(ctx->expr()));
    env_->assign(name, value);
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitForUpdateAssign(OpenDCADParser::ForUpdateAssignContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr value = toValue(visit(ctx->expr()));
    env_->assign(name, value);
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitForUpdateCompound(OpenDCADParser::ForUpdateCompoundContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr current = env_->lookup(name);
    ValuePtr rhs = toValue(visit(ctx->expr()));
    ValuePtr result;

    if (ctx->PLUS_EQ()) result = current->add(rhs);
    else if (ctx->MINUS_EQ()) result = current->subtract(rhs);
    else if (ctx->STAR_EQ()) result = current->multiply(rhs);
    else if (ctx->SLASH_EQ()) result = current->divide(rhs);

    env_->assign(name, result);
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitForUpdateIncr(OpenDCADParser::ForUpdateIncrContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    double val = env_->lookup(name)->asNumber();
    if (ctx->INCR())
        env_->assign(name, Value::makeNumber(val + 1));
    else
        env_->assign(name, Value::makeNumber(val - 1));
    return Value::makeNil();
}

// --- Chains & Calls ---

antlrcpp::Any Evaluator::visitChainExpr(OpenDCADParser::ChainExprContext* ctx) {
    return visit(ctx->postfix());
}

antlrcpp::Any Evaluator::visitPostFromCall(OpenDCADParser::PostFromCallContext* ctx) {
    auto* callCtx = ctx->c;
    std::string name = callCtx->IDENT()->getText();

    // Evaluate args (positional + named)
    std::vector<ValuePtr> positionalArgs;
    std::unordered_map<std::string, ValuePtr> namedArgs;
    if (callCtx->argList()) {
        for (auto* argCtx : callCtx->argList()->arg()) {
            if (auto* na = dynamic_cast<OpenDCADParser::NamedArgContext*>(argCtx)) {
                std::string argName = na->IDENT()->getText();
                namedArgs[argName] = toValue(visit(na->expr()));
            } else if (auto* pa = dynamic_cast<OpenDCADParser::PositionalArgContext*>(argCtx)) {
                positionalArgs.push_back(toValue(visit(pa->expr())));
            }
        }
    }

    ValuePtr current;

    // 1. Check builtins
    auto bit = builtins_.find(name);
    if (bit != builtins_.end()) {
        try {
            current = bit->second(positionalArgs);
        } catch (const EvalError& e) {
            throw EvalError(std::string(e.what()), locFrom(callCtx));
        }
    }
    // 2. Check user-defined functions
    else if (env_->has(name)) {
        ValuePtr fnVal;
        try { fnVal = env_->lookup(name); } catch (...) {}
        if (fnVal && fnVal->type() == ValueType::FUNCTION) {
            try {
                current = callFunction(fnVal->asFunction(), positionalArgs, namedArgs);
            } catch (const EvalError& e) {
                throw EvalError(std::string(e.what()), locFrom(callCtx));
            }
        } else {
            throw EvalError("'" + name + "' is not callable", locFrom(callCtx));
        }
    }
    // 3. Check shape factories
    else if (ShapeRegistry::instance().hasFactory(name)) {
        try {
            current = Value::makeShape(
                ShapeRegistry::instance().callFactory(name, positionalArgs));
        } catch (const EvalError& e) {
            throw EvalError(std::string(e.what()), locFrom(callCtx));
        } catch (const GeometryError& e) {
            throw EvalError(std::string(e.what()), locFrom(callCtx));
        }
    }
    else {
        throw EvalError("unknown function '" + name + "'", locFrom(callCtx));
    }

    return applyMethodChain(current, ctx->methodCall());
}

antlrcpp::Any Evaluator::visitPostFromVar(OpenDCADParser::PostFromVarContext* ctx) {
    std::string varName = ctx->var->getText();
    ValuePtr current;
    try {
        current = env_->lookup(varName);
    } catch (const EvalError&) {
        throw EvalError("undefined variable '" + varName + "'", locFrom(ctx));
    }
    return applyMethodChain(current, ctx->methodCall());
}

// --- Expressions ---

antlrcpp::Any Evaluator::visitLogicalOr(OpenDCADParser::LogicalOrContext* ctx) {
    ValuePtr left = toValue(visit(ctx->expr(0)));
    if (left->isTruthy()) return left;
    return toValue(visit(ctx->expr(1)));
}

antlrcpp::Any Evaluator::visitLogicalAnd(OpenDCADParser::LogicalAndContext* ctx) {
    ValuePtr left = toValue(visit(ctx->expr(0)));
    if (!left->isTruthy()) return left;
    return toValue(visit(ctx->expr(1)));
}

antlrcpp::Any Evaluator::visitEquality(OpenDCADParser::EqualityContext* ctx) {
    ValuePtr left = toValue(visit(ctx->expr(0)));
    ValuePtr right = toValue(visit(ctx->expr(1)));
    std::string op = ctx->children[1]->getText();
    try {
        if (op == "==") return left->equal(right);
        if (op == "!=") return left->notEqual(right);
    } catch (const EvalError& e) {
        throw EvalError(std::string(e.what()), locFrom(ctx));
    }
    throw EvalError("unknown equality operator '" + op + "'", locFrom(ctx));
}

antlrcpp::Any Evaluator::visitComparison(OpenDCADParser::ComparisonContext* ctx) {
    ValuePtr left = toValue(visit(ctx->expr(0)));
    ValuePtr right = toValue(visit(ctx->expr(1)));
    std::string op = ctx->children[1]->getText();
    try {
        if (op == "<") return left->lessThan(right);
        if (op == ">") return left->greaterThan(right);
        if (op == "<=") return left->lessEqual(right);
        if (op == ">=") return left->greaterEqual(right);
    } catch (const EvalError& e) {
        throw EvalError(std::string(e.what()), locFrom(ctx));
    }
    throw EvalError("unknown comparison operator '" + op + "'", locFrom(ctx));
}

antlrcpp::Any Evaluator::visitMulDivMod(OpenDCADParser::MulDivModContext* ctx) {
    ValuePtr left = toValue(visit(ctx->expr(0)));
    ValuePtr right = toValue(visit(ctx->expr(1)));

    std::string op = ctx->children[1]->getText();
    try {
        if (op == "*") return left->multiply(right);
        if (op == "/") return left->divide(right);
        if (op == "%") return left->modulo(right);
    } catch (const EvalError& e) {
        throw EvalError(std::string(e.what()), locFrom(ctx));
    }
    throw EvalError("unknown operator '" + op + "'", locFrom(ctx));
}

antlrcpp::Any Evaluator::visitAddSub(OpenDCADParser::AddSubContext* ctx) {
    ValuePtr left = toValue(visit(ctx->expr(0)));
    ValuePtr right = toValue(visit(ctx->expr(1)));

    std::string op = ctx->children[1]->getText();
    try {
        if (op == "+") return left->add(right);
        if (op == "-") return left->subtract(right);
    } catch (const EvalError& e) {
        throw EvalError(std::string(e.what()), locFrom(ctx));
    }
    throw EvalError("unknown operator '" + op + "'", locFrom(ctx));
}

antlrcpp::Any Evaluator::visitLogicalNot(OpenDCADParser::LogicalNotContext* ctx) {
    ValuePtr val = toValue(visit(ctx->expr()));
    return val->logicalNot();
}

antlrcpp::Any Evaluator::visitUnaryNeg(OpenDCADParser::UnaryNegContext* ctx) {
    ValuePtr val = toValue(visit(ctx->expr()));
    try {
        return val->negate();
    } catch (const EvalError& e) {
        throw EvalError(std::string(e.what()), locFrom(ctx));
    }
}

antlrcpp::Any Evaluator::visitIndexAccess(OpenDCADParser::IndexAccessContext* ctx) {
    ValuePtr collection = toValue(visit(ctx->expr(0)));
    ValuePtr indexVal = toValue(visit(ctx->expr(1)));
    int index = static_cast<int>(indexVal->asNumber());

    if (collection->type() == ValueType::LIST) {
        return collection->listGet(index);
    }
    if (collection->type() == ValueType::VECTOR) {
        const auto& vec = collection->asVector();
        if (index < 0 || index >= static_cast<int>(vec.size()))
            throw EvalError("vector index out of bounds", locFrom(ctx));
        return Value::makeNumber(vec[index]);
    }
    throw EvalError("indexing not supported on " + collection->typeName(), locFrom(ctx));
}

antlrcpp::Any Evaluator::visitPrimaryExpr(OpenDCADParser::PrimaryExprContext* ctx) {
    return visit(ctx->primary());
}

antlrcpp::Any Evaluator::visitPrimary(OpenDCADParser::PrimaryContext* ctx) {
    if (ctx->NUMBER()) {
        return ValuePtr(Value::makeNumber(std::stod(ctx->NUMBER()->getText())));
    }
    if (ctx->STRING()) {
        std::string raw = ctx->STRING()->getText();
        std::string content = raw.substr(1, raw.size() - 2);
        return ValuePtr(Value::makeString(content));
    }
    if (ctx->TRUE()) {
        return ValuePtr(Value::makeBool(true));
    }
    if (ctx->FALSE()) {
        return ValuePtr(Value::makeBool(false));
    }
    if (ctx->NIL_LIT()) {
        return ValuePtr(Value::makeNil());
    }
    if (ctx->listLiteral()) {
        return visit(ctx->listLiteral());
    }
    if (ctx->vectorLiteral()) {
        return visit(ctx->vectorLiteral());
    }
    if (ctx->postfix()) {
        return visit(ctx->postfix());
    }
    if (ctx->IDENT()) {
        std::string name = ctx->IDENT()->getText();
        try {
            return env_->lookup(name);
        } catch (const EvalError&) {
            throw EvalError("undefined variable '" + name + "'", locFrom(ctx));
        }
    }
    if (ctx->expr()) {
        return visit(ctx->expr());
    }
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitVectorLiteral(OpenDCADParser::VectorLiteralContext* ctx) {
    std::vector<double> components;
    for (auto* expr : ctx->expr()) {
        ValuePtr val = toValue(visit(expr));
        try {
            components.push_back(val->asNumber());
        } catch (const EvalError&) {
            throw EvalError("vector component must be a number, got " + val->typeName(),
                            locFrom(ctx));
        }
    }
    return ValuePtr(Value::makeVector(components));
}

antlrcpp::Any Evaluator::visitListLiteral(OpenDCADParser::ListLiteralContext* ctx) {
    std::vector<ValuePtr> elements;
    for (auto* expr : ctx->expr()) {
        elements.push_back(toValue(visit(expr)));
    }
    return ValuePtr(Value::makeList(elements));
}

} // namespace opendcad
