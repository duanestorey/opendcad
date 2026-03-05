#include "Evaluator.h"
#include "ShapeRegistry.h"
#include "Debug.h"

namespace opendcad {

Evaluator::Evaluator()
    : env_(std::make_shared<Environment>()) {}

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
    for (auto* expr : ctx->expr()) {
        args.push_back(toValue(visit(expr)));
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

// --- Visitor overrides ---

antlrcpp::Any Evaluator::visitProgram(OpenDCADParser::ProgramContext* ctx) {
    for (auto* stmt : ctx->stmt()) {
        visit(stmt);
    }
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitLetAlias(OpenDCADParser::LetAliasContext* ctx) {
    std::string name = ctx->IDENT(0)->getText();
    std::string source = ctx->IDENT(1)->getText();
    try {
        ValuePtr value = env_->lookup(source);
        env_->define(name, value);
    } catch (const EvalError&) {
        throw EvalError("undefined variable '" + source + "'", locFrom(ctx));
    }
    DEBUG_INFO("let " << name << " = " << source << " (alias)");
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitLetChain(OpenDCADParser::LetChainContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr value = toValue(visit(ctx->chainExpr()));
    env_->define(name, value);
    DEBUG_INFO("let " << name << " = " << value->toString());
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitLetValue(OpenDCADParser::LetValueContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr value = toValue(visit(ctx->expr()));
    env_->define(name, value);
    DEBUG_INFO("let " << name << " = " << value->toString());
    return Value::makeNil();
}

antlrcpp::Any Evaluator::visitExportStmt(OpenDCADParser::ExportStmtContext* ctx) {
    std::string name = ctx->IDENT()->getText();
    ValuePtr value = toValue(visit(ctx->expr()));
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

antlrcpp::Any Evaluator::visitChainExpr(OpenDCADParser::ChainExprContext* ctx) {
    return visit(ctx->postfix());
}

antlrcpp::Any Evaluator::visitPostFromCall(OpenDCADParser::PostFromCallContext* ctx) {
    auto* callCtx = ctx->c;
    std::string factoryName = callCtx->IDENT()->getText();
    auto args = evaluateArgs(callCtx->argList());

    ShapePtr shape;
    try {
        shape = ShapeRegistry::instance().callFactory(factoryName, args);
    } catch (const EvalError& e) {
        throw EvalError(std::string(e.what()), locFrom(callCtx));
    } catch (const GeometryError& e) {
        throw EvalError(std::string(e.what()), locFrom(callCtx));
    }

    ValuePtr current = Value::makeShape(shape);
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

antlrcpp::Any Evaluator::visitUnaryNeg(OpenDCADParser::UnaryNegContext* ctx) {
    ValuePtr val = toValue(visit(ctx->expr()));
    try {
        return val->negate();
    } catch (const EvalError& e) {
        throw EvalError(std::string(e.what()), locFrom(ctx));
    }
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
        // Strip surrounding quotes
        std::string content = raw.substr(1, raw.size() - 2);
        return ValuePtr(Value::makeString(content));
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

} // namespace opendcad
