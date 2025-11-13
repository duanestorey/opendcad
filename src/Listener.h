#pragma once
#include <iostream>
#include <string>
#include "antlr4-runtime.h"
#include "OpenDCADLexer.h"
#include "OpenDCADParser.h"
#include "OpenDCADBaseVisitor.h"
#include "Debug.h"

using namespace OpenDCAD;

struct TraceVisitor : public OpenDCADBaseVisitor {
    int depth = 0;
    std::string indent() const { return std::string(depth * 2, ' '); }

    // program : stmt* EOF
    antlrcpp::Any visitProgram(OpenDCADParser::ProgramContext* ctx) override {
        DEBUG_VERBOSE( "Program" );
        depth++;
        for (auto* s : ctx->stmt()) visit(s);
        depth--;
        return nullptr;
    }

    // ---------- letStmt (labeled alts) ----------

    // let a = b;
    antlrcpp::Any visitLetAlias(OpenDCADParser::LetAliasContext* ctx) override {
        DEBUG_VERBOSE( indent() << "Let " << ctx->IDENT(0)->getText() << " = " << ctx->IDENT(1)->getText() << " (alias)" );
        return nullptr;
    }

    // let a = bin(...).fillet(...);
    antlrcpp::Any visitLetChain(OpenDCADParser::LetChainContext* ctx) override {
        DEBUG_VERBOSE( indent() << "Let " << ctx->IDENT()->getText() << " =" );
        depth++; visit(ctx->chainExpr()); depth--;
        return nullptr;
    }

    // let a = expr;   (numbers / vectors / even chains allowed by grammar)
    antlrcpp::Any visitLetValue(OpenDCADParser::LetValueContext* ctx) override {
        DEBUG_VERBOSE( indent() << "Let " << ctx->IDENT()->getText() << " = (expr)" );
        depth++; visit(ctx->expr()); depth--;
        return nullptr;
    }

    // ---------- export ----------

    // export expr as IDENT ;
    antlrcpp::Any visitExportStmt(OpenDCADParser::ExportStmtContext* ctx) override {
        DEBUG_VERBOSE( indent() << "Export as " << ctx->IDENT()->getText() << " from:" );
        depth++; visit(ctx->expr()); depth--;
        return nullptr;
    }

    // ---------- chains ----------

    antlrcpp::Any visitChainExpr(OpenDCADParser::ChainExprContext* ctx) override {
        return visitChildren(ctx); // will hit postFromCall / postFromVar
    }

    // postfix: c=call ('.' methodCall)*   # postFromCall
    antlrcpp::Any visitPostFromCall(OpenDCADParser::PostFromCallContext* ctx) override {
        DEBUG_VERBOSE( indent() << "Chain" );
        depth++;

        auto* c = ctx->c;
        size_t argc = c->argList()->expr().size();
        DEBUG_VERBOSE( indent() << "Seed: " << c->IDENT()->getText() << "(" << argc << " args)" );

        for (auto* m : ctx->methodCall()) {
            size_t margc = m->argList()->expr().size();
            DEBUG_VERBOSE( indent() << ". " << m->IDENT()->getText() << "(" << margc << " args)" );
        }

        depth--;
        return nullptr;
    }

    antlrcpp::Any visitPostFromVar(OpenDCADParser::PostFromVarContext* ctx) override {
        DEBUG_VERBOSE( indent() << "Chain" );
        depth++;

        for (auto* m : ctx->methodCall()) {
            size_t margc = m->argList()->expr().size();
        }

        depth--;
        return nullptr;
    }

    antlrcpp::Any visitCall(OpenDCADParser::CallContext* ctx) override {
        // Normally printed by postFromCall; keep quiet to avoid duplication.
        return nullptr;
    }

    antlrcpp::Any visitMethodCall(OpenDCADParser::MethodCallContext* ctx) override {
        // Printed within postFromCall/postFromVar; nothing to do here.
        return nullptr;
    }

    // ---------- expressions / literals (just to show structure) ----------
    antlrcpp::Any visitPrimaryExpr(OpenDCADParser::PrimaryExprContext* ctx) override {
        return visit(ctx->primary());
    }

    antlrcpp::Any visitPrimary(OpenDCADParser::PrimaryContext* ctx) override {
        if (ctx->NUMBER()) {
            DEBUG_VERBOSE( indent() << "Number " << ctx->NUMBER()->getText() );
            return nullptr;
        }
        if (ctx->vectorLiteral()) return visit(ctx->vectorLiteral());
        if (ctx->postfix())       return visit(ctx->postfix());
        if (ctx->IDENT()) {       // <-- new: bare variable as expression
            DEBUG_VERBOSE( indent() << "Var " << ctx->IDENT()->getText() );
            return nullptr;
        }
        if (ctx->expr())          return visit(ctx->expr());
        return nullptr;
    }

    // [x,y] or [x,y,z]
    antlrcpp::Any visitVectorLiteral(OpenDCADParser::VectorLiteralContext* ctx) override {
        DEBUG_VERBOSE( indent() << "Vector[" << ctx->expr().size() << "]" );
        depth++;
        for (auto* e : ctx->expr()) visit(e);
        depth--;
        return nullptr;
    }

    // arithmetic nodes – just recurse so numbers/vectors under them get shown
    antlrcpp::Any visitMulDivMod(OpenDCADParser::MulDivModContext* ctx) override {
        return visitChildren(ctx);
    }
    antlrcpp::Any visitAddSub(OpenDCADParser::AddSubContext* ctx) override {
        return visitChildren(ctx);
    }
    antlrcpp::Any visitUnaryNeg(OpenDCADParser::UnaryNegContext* ctx) override {
        return visitChildren(ctx);
    }
};