#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
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

// Exception used to unwind from return statements inside functions
struct ReturnValue {
    ValuePtr value;
};

class Evaluator : public OpenDCADBaseVisitor {
public:
    Evaluator();

    void evaluate(OpenDCADParser::ProgramContext* tree,
                  const std::string& filename = "");

    const std::vector<ExportEntry>& exports() const { return exports_; }

    // Visitor overrides
    antlrcpp::Any visitProgram(OpenDCADParser::ProgramContext* ctx) override;

    // Declarations
    antlrcpp::Any visitLetStmt(OpenDCADParser::LetStmtContext* ctx) override;
    antlrcpp::Any visitConstStmt(OpenDCADParser::ConstStmtContext* ctx) override;
    antlrcpp::Any visitAssignVar(OpenDCADParser::AssignVarContext* ctx) override;
    antlrcpp::Any visitAssignIndex(OpenDCADParser::AssignIndexContext* ctx) override;
    antlrcpp::Any visitCompoundAssignStmt(OpenDCADParser::CompoundAssignStmtContext* ctx) override;
    antlrcpp::Any visitPostfixIncrStmt(OpenDCADParser::PostfixIncrStmtContext* ctx) override;
    antlrcpp::Any visitExportStmt(OpenDCADParser::ExportStmtContext* ctx) override;
    antlrcpp::Any visitExprStmt(OpenDCADParser::ExprStmtContext* ctx) override;

    // Control flow
    antlrcpp::Any visitBlock(OpenDCADParser::BlockContext* ctx) override;
    antlrcpp::Any visitIfStmt(OpenDCADParser::IfStmtContext* ctx) override;
    antlrcpp::Any visitForCStyle(OpenDCADParser::ForCStyleContext* ctx) override;
    antlrcpp::Any visitForEach(OpenDCADParser::ForEachContext* ctx) override;
    antlrcpp::Any visitWhileStmt(OpenDCADParser::WhileStmtContext* ctx) override;
    antlrcpp::Any visitReturnStmt(OpenDCADParser::ReturnStmtContext* ctx) override;

    // Functions
    antlrcpp::Any visitFnDecl(OpenDCADParser::FnDeclContext* ctx) override;

    // For loop init/update helpers
    antlrcpp::Any visitForInitLet(OpenDCADParser::ForInitLetContext* ctx) override;
    antlrcpp::Any visitForInitAssign(OpenDCADParser::ForInitAssignContext* ctx) override;
    antlrcpp::Any visitForUpdateAssign(OpenDCADParser::ForUpdateAssignContext* ctx) override;
    antlrcpp::Any visitForUpdateCompound(OpenDCADParser::ForUpdateCompoundContext* ctx) override;
    antlrcpp::Any visitForUpdateIncr(OpenDCADParser::ForUpdateIncrContext* ctx) override;

    // Chains & calls
    antlrcpp::Any visitChainExpr(OpenDCADParser::ChainExprContext* ctx) override;
    antlrcpp::Any visitPostFromCall(OpenDCADParser::PostFromCallContext* ctx) override;
    antlrcpp::Any visitPostFromVar(OpenDCADParser::PostFromVarContext* ctx) override;

    // Expressions
    antlrcpp::Any visitLogicalOr(OpenDCADParser::LogicalOrContext* ctx) override;
    antlrcpp::Any visitLogicalAnd(OpenDCADParser::LogicalAndContext* ctx) override;
    antlrcpp::Any visitEquality(OpenDCADParser::EqualityContext* ctx) override;
    antlrcpp::Any visitComparison(OpenDCADParser::ComparisonContext* ctx) override;
    antlrcpp::Any visitMulDivMod(OpenDCADParser::MulDivModContext* ctx) override;
    antlrcpp::Any visitAddSub(OpenDCADParser::AddSubContext* ctx) override;
    antlrcpp::Any visitLogicalNot(OpenDCADParser::LogicalNotContext* ctx) override;
    antlrcpp::Any visitUnaryNeg(OpenDCADParser::UnaryNegContext* ctx) override;
    antlrcpp::Any visitIndexAccess(OpenDCADParser::IndexAccessContext* ctx) override;
    antlrcpp::Any visitPrimaryExpr(OpenDCADParser::PrimaryExprContext* ctx) override;
    antlrcpp::Any visitPrimary(OpenDCADParser::PrimaryContext* ctx) override;
    antlrcpp::Any visitVectorLiteral(OpenDCADParser::VectorLiteralContext* ctx) override;
    antlrcpp::Any visitListLiteral(OpenDCADParser::ListLiteralContext* ctx) override;

    EnvironmentPtr environment() const { return env_; }

private:
    using BuiltinFn = std::function<ValuePtr(const std::vector<ValuePtr>&)>;

    EnvironmentPtr env_;
    std::vector<ExportEntry> exports_;
    std::string filename_;
    std::unordered_map<std::string, BuiltinFn> builtins_;
    static constexpr int MAX_ITERATIONS = 100000;

    void registerBuiltins();
    ValuePtr callFunction(std::shared_ptr<struct FunctionDef> fn,
                          const std::vector<ValuePtr>& positionalArgs,
                          const std::unordered_map<std::string, ValuePtr>& namedArgs);

    SourceLoc locFrom(antlr4::ParserRuleContext* ctx) const;
    ValuePtr toValue(antlrcpp::Any result);
    std::vector<ValuePtr> evaluateArgs(OpenDCADParser::ArgListContext* ctx);
    ValuePtr applyMethodChain(ValuePtr current,
                              const std::vector<OpenDCADParser::MethodCallContext*>& methods);
};

} // namespace opendcad
