#pragma once

#include <string>
#include <vector>
#include "antlr4-runtime.h"
#include "OpenDCADBaseVisitor.h"
#include "OpenDCADParser.h"
#include "Value.h"
#include "Environment.h"
#include "Error.h"

using namespace OpenDCAD;

namespace opendcad {

struct ExportEntry {
    std::string name;
    ShapePtr shape;
};

class Evaluator : public OpenDCADBaseVisitor {
public:
    Evaluator();

    void evaluate(OpenDCADParser::ProgramContext* tree,
                  const std::string& filename = "");

    const std::vector<ExportEntry>& exports() const { return exports_; }

    // Visitor overrides
    antlrcpp::Any visitProgram(OpenDCADParser::ProgramContext* ctx) override;

    antlrcpp::Any visitLetAlias(OpenDCADParser::LetAliasContext* ctx) override;
    antlrcpp::Any visitLetChain(OpenDCADParser::LetChainContext* ctx) override;
    antlrcpp::Any visitLetValue(OpenDCADParser::LetValueContext* ctx) override;

    antlrcpp::Any visitExportStmt(OpenDCADParser::ExportStmtContext* ctx) override;

    antlrcpp::Any visitChainExpr(OpenDCADParser::ChainExprContext* ctx) override;
    antlrcpp::Any visitPostFromCall(OpenDCADParser::PostFromCallContext* ctx) override;
    antlrcpp::Any visitPostFromVar(OpenDCADParser::PostFromVarContext* ctx) override;

    antlrcpp::Any visitMulDivMod(OpenDCADParser::MulDivModContext* ctx) override;
    antlrcpp::Any visitAddSub(OpenDCADParser::AddSubContext* ctx) override;
    antlrcpp::Any visitUnaryNeg(OpenDCADParser::UnaryNegContext* ctx) override;
    antlrcpp::Any visitPrimaryExpr(OpenDCADParser::PrimaryExprContext* ctx) override;
    antlrcpp::Any visitPrimary(OpenDCADParser::PrimaryContext* ctx) override;
    antlrcpp::Any visitVectorLiteral(OpenDCADParser::VectorLiteralContext* ctx) override;

    EnvironmentPtr environment() const { return env_; }

private:
    EnvironmentPtr env_;
    std::vector<ExportEntry> exports_;
    std::string filename_;

    SourceLoc locFrom(antlr4::ParserRuleContext* ctx) const;
    ValuePtr toValue(antlrcpp::Any result);
    std::vector<ValuePtr> evaluateArgs(OpenDCADParser::ArgListContext* ctx);
    ValuePtr applyMethodChain(ValuePtr current,
                              const std::vector<OpenDCADParser::MethodCallContext*>& methods);
};

} // namespace opendcad
