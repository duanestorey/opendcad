# Phase 4: Full Parser & Hot-Loading

## Goal

Implement the complete parser pipeline for all Phase 3 language features, introduce an explicit AST layer (decoupling evaluation from the ANTLR parse tree), expand the Evaluator to support functions, modules, imports, control flow, selectors, and DFM constructs, and add a hot-loading architecture so the viewer rebuilds geometry when source files change on disk.

## Prerequisites

- Phase 0 complete: Evaluator, Value, Environment, ShapeRegistry are working.
- Phase 1-2 complete: All primitives (sphere, cone, wedge, polyhedron, etc.) and face modeling operations are registered in ShapeRegistry.
- Phase 3 complete: Language design is finalized. The syntax and semantics described below match the Phase 3 language specification.

---

## 1. Grammar Overhaul (`grammar/OpenDCAD.g4`)

The existing grammar supports only `let`, `export`, method chains, arithmetic, vectors, and identifiers. Phase 4 expands it to cover the full language: `const`, `fn`, `module`, `import`, `for`, `if/else`, `return`, named arguments, comparison and logical operators, `@` selectors, `sketch` blocks, `@process` declarations, `dfm_check` statements, unit-suffixed numbers, and string literals.

### Complete ANTLR4 Grammar

```antlr
grammar OpenDCAD;

// =====================================================================
//  Program
// =====================================================================

program
  : topLevelStmt* EOF
  ;

topLevelStmt
  : importStmt
  | fnDef
  | moduleDef
  | processDef
  | stmt
  ;

// =====================================================================
//  Import
// =====================================================================

// import "./path/to/file.dcad";
// import "./utils.dcad" as utils;
// import { foo, bar } from "./lib.dcad";
// import std::threads;

importStmt
  : 'import' STRING ';'                                          # importPlain
  | 'import' STRING 'as' IDENT ';'                               # importAlias
  | 'import' '{' importList '}' 'from' STRING ';'                # importDestructure
  | 'import' stdPath ';'                                          # importStd
  ;

importList
  : IDENT (',' IDENT)*
  ;

stdPath
  : 'std' '::' IDENT ('::' IDENT)*
  ;

// =====================================================================
//  Function & Module Definitions
// =====================================================================

// fn name(param1, param2 = defaultExpr) { ... }
fnDef
  : 'fn' IDENT '(' paramList? ')' block
  ;

// module name(param1, param2 = defaultExpr) { ... }
moduleDef
  : 'module' IDENT '(' paramList? ')' block
  ;

paramList
  : param (',' param)*
  ;

param
  : IDENT ('=' expr)?
  ;

// =====================================================================
//  @process Declarations (DFM)
// =====================================================================

// @process fdm_print { layer_height: 0.2mm, nozzle_diameter: 0.4mm }
processDef
  : '@process' IDENT '{' processFieldList? '}'
  ;

processFieldList
  : processField (',' processField)*  ','?
  ;

processField
  : IDENT ':' expr
  ;

// =====================================================================
//  Statements
// =====================================================================

stmt
  : letStmt ';'
  | constStmt ';'
  | assignStmt ';'
  | exportStmt ';'
  | returnStmt ';'
  | dfmCheckStmt ';'
  | exprStmt ';'
  | forStmt
  | ifStmt
  ;

// let x = expr;
letStmt
  : 'let' IDENT '=' expr
  ;

// const TAU = 6.2831853;
constStmt
  : 'const' IDENT '=' expr
  ;

// x = expr;   (reassignment -- only for mutable let bindings)
assignStmt
  : IDENT '=' expr
  ;

// export expr as name;
exportStmt
  : 'export' expr 'as' IDENT
  ;

// return expr;
// return;
returnStmt
  : 'return' expr?
  ;

// dfm_check part_name against process_name;
dfmCheckStmt
  : 'dfm_check' expr 'against' IDENT
  ;

// bare expression as statement (for side-effecting method calls, etc.)
exprStmt
  : expr
  ;

// =====================================================================
//  For Loop
// =====================================================================

// for i in 0..8 { ... }
// for i in range(0, 8) { ... }
// for item in collection { ... }
forStmt
  : 'for' IDENT 'in' forIterable block
  ;

forIterable
  : expr '..' expr                                               # rangeIterable
  | expr                                                         # exprIterable
  ;

// =====================================================================
//  If / Else
// =====================================================================

ifStmt
  : 'if' expr block ('else' 'if' expr block)* ('else' block)?
  ;

// =====================================================================
//  Block
// =====================================================================

block
  : '{' stmt* '}'
  ;

// =====================================================================
//  Expressions (precedence from lowest to highest)
// =====================================================================

expr
  : expr '?' expr ':' expr                                       # ternaryExpr
  | expr 'or' expr                                               # logicOr
  | expr 'and' expr                                              # logicAnd
  | 'not' expr                                                   # logicNot
  | expr compOp expr                                             # comparison
  | expr ('+' | '-') expr                                        # addSub
  | expr ('*' | '/' | '%') expr                                  # mulDivMod
  | '-' expr                                                     # unaryNeg
  | expr '.' IDENT '(' argList? ')'                              # methodCallExpr
  | expr '.' IDENT                                               # memberAccess
  | postfixExpr                                                  # postfixPrimary
  ;

compOp
  : '==' | '!=' | '<' | '>' | '<=' | '>='
  ;

postfixExpr
  : IDENT '(' argList? ')'                                       # funcCallExpr
  | primary                                                      # primaryAtom
  ;

// =====================================================================
//  Argument List (positional and named)
// =====================================================================

argList
  : argument (',' argument)*
  ;

argument
  : IDENT ':' expr                                               # namedArg
  | expr                                                         # positionalArg
  ;

// =====================================================================
//  Primary Expressions
// =====================================================================

primary
  : NUMBER unitSuffix?                                           # numberLit
  | STRING                                                       # stringLit
  | 'true'                                                       # boolTrue
  | 'false'                                                      # boolFalse
  | vectorLiteral                                                # vecLit
  | selectorExpr                                                 # selectorPrimary
  | sketchExpr                                                   # sketchPrimary
  | IDENT                                                        # varRef
  | '(' expr ')'                                                 # parenExpr
  ;

// =====================================================================
//  Vector Literals
// =====================================================================

// [x, y] or [x, y, z] or [x, y, z, w, ...]
vectorLiteral
  : '[' expr (',' expr)* ']'
  ;

// =====================================================================
//  Selector Expressions
// =====================================================================

// @faces(top)
// @edges(fillet, radius > 2mm)
// @faces(normal == [0,0,1])
selectorExpr
  : '@' IDENT '(' selectorArgList? ')'
  ;

selectorArgList
  : selectorArg (',' selectorArg)*
  ;

selectorArg
  : IDENT                                                        # selectorName
  | IDENT compOp expr                                            # selectorFilter
  ;

// =====================================================================
//  Sketch Blocks
// =====================================================================

// sketch() { line_to(10mm, 0); arc_to(20mm, 10mm, radius: 5mm); close(); }
sketchExpr
  : 'sketch' '(' argList? ')' '{' sketchStmt* '}'
  ;

sketchStmt
  : IDENT '(' argList? ')' ';'
  ;

// =====================================================================
//  Unit Suffixes
// =====================================================================

unitSuffix
  : 'mm' | 'cm' | 'm' | 'in' | 'ft' | 'thou' | 'deg' | 'rad'
  ;

// =====================================================================
//  Lexer Rules
// =====================================================================

// Keywords (must appear before IDENT to take priority)
// ANTLR handles keyword-vs-identifier via implicit token rules in the
// parser.  The keywords 'let', 'const', 'fn', 'module', 'import', 'for',
// 'in', 'if', 'else', 'return', 'export', 'as', 'from', 'and', 'or',
// 'not', 'true', 'false', 'sketch', 'std', 'dfm_check' are all
// represented as inline string literals in the parser rules above.
// ANTLR auto-generates implicit lexer tokens for them, which take
// priority over IDENT because they appear first in the combined grammar.

IDENT
  : [a-zA-Z_] [a-zA-Z_0-9]*
  ;

NUMBER
  : [0-9]+ ('.' [0-9]+)? ( [eE] [+-]? [0-9]+ )?
  | '.' [0-9]+ ( [eE] [+-]? [0-9]+ )?
  ;

STRING
  : '"' (~["\\\r\n] | '\\' .)* '"'
  ;

WS
  : [ \t\r\n]+ -> channel(HIDDEN)
  ;

LINE_COMMENT
  : '//' ~[\r\n]* -> channel(HIDDEN)
  ;

BLOCK_COMMENT
  : '/*' .*? '*/' -> channel(HIDDEN)
  ;
```

### Grammar Design Notes

**Keyword handling.** ANTLR4 combined grammars create implicit lexer tokens for string literals used in parser rules (e.g., `'let'`, `'fn'`, `'for'`). These implicit tokens are matched before `IDENT`, so `let` is never tokenized as an identifier. No explicit keyword lexer rules are needed.

**Unit suffix ambiguity.** Unit suffixes like `mm`, `cm`, `in` would normally be tokenized as `IDENT`. The `unitSuffix` parser rule matches specific identifiers in the number context. The parser resolves this because `NUMBER unitSuffix?` tries to consume the following `IDENT` if it matches a known unit. For robust handling, the ASTBuilder validates that the IDENT text is one of the recognized unit strings and reports an error otherwise.

**Operator precedence.** The `expr` rule is ordered from lowest to highest precedence: ternary, `or`, `and`, `not`, comparison, additive, multiplicative, unary negation, method calls, member access, postfix (function calls), primary. ANTLR4's left-recursive expression handling resolves this correctly based on alternative order.

**Named arguments.** The `argument` rule uses labeled alternatives (`namedArg` and `positionalArg`) so the ASTBuilder can distinguish `foo(x: 10)` (named) from `foo(10)` (positional). Named arguments must follow all positional arguments -- this constraint is enforced in the ASTBuilder, not the grammar, to produce better error messages.

**`for` loop range syntax.** `0..8` is parsed as `expr '..' expr`. The `..` token does not conflict with the `NUMBER` rule because `NUMBER` requires digits after the dot. `0..8` tokenizes as `NUMBER('0') '..' NUMBER('8')`.

**`@` selectors.** The `@` character is not used elsewhere in the grammar. `selectorExpr` handles `@faces(...)`, `@edges(...)`, `@vertices(...)` etc. Inside the parentheses, arguments can be bare identifiers (semantic names like `top`, `bottom`) or filter expressions with comparison operators.

**Sketch blocks.** `sketch() { ... }` is a primary expression that produces a 2D profile. Inside the block, each statement is a drawing command (function call with semicolon). This simplified grammar covers `line_to`, `arc_to`, `bezier_to`, `close`, `move_to`, etc. without enumerating them -- the Evaluator validates command names.

---

## 2. AST Layer (`src/parser/AST.h`)

### Motivation

In Phase 0, the Evaluator walks the ANTLR parse tree directly. This couples evaluation to ANTLR's concrete syntax tree structure, which has several problems:

1. **Fragile coupling.** Any grammar refactor (renaming alternatives, reordering rules) requires Evaluator changes.
2. **No source location on expressions.** ANTLR context objects carry locations, but accessing them requires casting through `getStart()` on every evaluation.
3. **Difficult to serialize/cache.** ANTLR parse trees cannot be easily saved and restored.
4. **Harder to optimize.** Multi-pass analysis (constant folding, type checking) is awkward on a concrete parse tree.

The AST is an internal, grammar-independent representation. The ASTBuilder visitor (Section 3) converts the parse tree into the AST in a single pass. The Evaluator then walks the AST.

### Source Location

Every AST node carries a `SourceLoc` for error reporting:

```cpp
// Already defined in src/core/Error.h (Phase 0):
struct SourceLoc {
    std::string filename;
    size_t line = 0;
    size_t col = 0;
    std::string str() const;  // "file.dcad:12:5"
};
```

### Node Base Class and Smart Pointers

```cpp
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include "Error.h"

namespace opendcad {
namespace ast {

// Forward declarations
struct Node;
struct Program;
struct FnDef;
struct ModuleDef;
struct ImportStmt;
struct ConstDecl;
struct LetDecl;
struct Assignment;
struct ForStmt;
struct IfStmt;
struct ReturnStmt;
struct ExportStmt;
struct ExprStmt;
struct DfmCheckStmt;
struct ProcessDef;
struct NumberLit;
struct StringLit;
struct BoolLit;
struct VectorLit;
struct VarRef;
struct BinaryOp;
struct UnaryOp;
struct FuncCall;
struct MethodCall;
struct MemberAccess;
struct TernaryExpr;
struct SelectorExpr;
struct SketchExpr;

// Smart pointer aliases
using NodePtr       = std::unique_ptr<Node>;
using ExprPtr       = std::unique_ptr<Node>;   // Expression nodes
using StmtPtr       = std::unique_ptr<Node>;   // Statement nodes

// =====================================================================
//  Base
// =====================================================================

struct Node {
    SourceLoc loc;
    virtual ~Node() = default;

    enum class Kind {
        Program,
        // Definitions
        FnDef, ModuleDef, ProcessDef,
        // Statements
        ImportStmt, ConstDecl, LetDecl, Assignment,
        ForStmt, IfStmt, ReturnStmt, ExportStmt, ExprStmt,
        DfmCheckStmt,
        // Expressions
        NumberLit, StringLit, BoolLit, VectorLit, VarRef,
        BinaryOp, UnaryOp, FuncCall, MethodCall, MemberAccess,
        TernaryExpr, SelectorExpr, SketchExpr,
    };

    virtual Kind kind() const = 0;
};

// =====================================================================
//  Utility Types
// =====================================================================

enum class BinOpKind {
    Add, Sub, Mul, Div, Mod,
    Eq, Neq, Lt, Gt, Lte, Gte,
    And, Or,
};

enum class UnaryOpKind {
    Neg,    // -expr
    Not,    // not expr
};

enum class Unit {
    None,
    Mm, Cm, M,
    In, Ft, Thou,
    Deg, Rad,
};

// Parameter with optional default value
struct ParamDef {
    std::string name;
    ExprPtr defaultValue;   // nullptr if no default
    SourceLoc loc;
};

// Argument in a function/method call
struct Argument {
    std::string name;       // empty string for positional args
    ExprPtr value;
    SourceLoc loc;
};

// Selector argument
struct SelectorArg {
    std::string name;                       // e.g. "top", "radius"
    std::optional<BinOpKind> filterOp;      // e.g. Gt for ">"
    ExprPtr filterValue;                    // nullptr if bare name
    SourceLoc loc;
};

// Import variant data
enum class ImportKind {
    Plain,          // import "path";
    Alias,          // import "path" as name;
    Destructure,    // import { a, b } from "path";
    Std,            // import std::threads;
};

// =====================================================================
//  Program (root)
// =====================================================================

struct Program : Node {
    std::vector<StmtPtr> body;      // top-level statements and definitions

    Kind kind() const override { return Kind::Program; }
};

// =====================================================================
//  Definitions
// =====================================================================

struct FnDef : Node {
    std::string name;
    std::vector<ParamDef> params;
    std::vector<StmtPtr> body;

    Kind kind() const override { return Kind::FnDef; }
};

struct ModuleDef : Node {
    std::string name;
    std::vector<ParamDef> params;
    std::vector<StmtPtr> body;

    Kind kind() const override { return Kind::ModuleDef; }
};

struct ProcessDef : Node {
    std::string name;
    std::vector<std::pair<std::string, ExprPtr>> fields;  // name-value pairs

    Kind kind() const override { return Kind::ProcessDef; }
};

// =====================================================================
//  Statements
// =====================================================================

struct ImportStmt : Node {
    ImportKind importKind;
    std::string path;               // file path or std path like "std::threads"
    std::string alias;              // for ImportKind::Alias
    std::vector<std::string> names; // for ImportKind::Destructure

    Kind kind() const override { return Kind::ImportStmt; }
};

struct ConstDecl : Node {
    std::string name;
    ExprPtr value;

    Kind kind() const override { return Kind::ConstDecl; }
};

struct LetDecl : Node {
    std::string name;
    ExprPtr value;

    Kind kind() const override { return Kind::LetDecl; }
};

struct Assignment : Node {
    std::string name;
    ExprPtr value;

    Kind kind() const override { return Kind::Assignment; }
};

struct ForStmt : Node {
    std::string varName;
    ExprPtr rangeStart;     // non-null for range iterable (start..end)
    ExprPtr rangeEnd;       // non-null for range iterable
    ExprPtr iterable;       // non-null for expression iterable (collection)
    std::vector<StmtPtr> body;

    bool isRange() const { return rangeStart != nullptr; }

    Kind kind() const override { return Kind::ForStmt; }
};

struct IfStmt : Node {
    // conditions[i] pairs with bodies[i]
    // if bodies.size() == conditions.size() + 1, the last body is the else branch
    std::vector<ExprPtr> conditions;
    std::vector<std::vector<StmtPtr>> bodies;

    bool hasElse() const { return bodies.size() == conditions.size() + 1; }

    Kind kind() const override { return Kind::IfStmt; }
};

struct ReturnStmt : Node {
    ExprPtr value;          // nullptr for bare "return;"

    Kind kind() const override { return Kind::ReturnStmt; }
};

struct ExportStmt : Node {
    ExprPtr value;
    std::string name;

    Kind kind() const override { return Kind::ExportStmt; }
};

struct ExprStmt : Node {
    ExprPtr expr;

    Kind kind() const override { return Kind::ExprStmt; }
};

struct DfmCheckStmt : Node {
    ExprPtr part;
    std::string processName;

    Kind kind() const override { return Kind::DfmCheckStmt; }
};

// =====================================================================
//  Expressions
// =====================================================================

struct NumberLit : Node {
    double value;
    Unit unit = Unit::None;

    Kind kind() const override { return Kind::NumberLit; }
};

struct StringLit : Node {
    std::string value;      // unescaped content (without quotes)

    Kind kind() const override { return Kind::StringLit; }
};

struct BoolLit : Node {
    bool value;

    Kind kind() const override { return Kind::BoolLit; }
};

struct VectorLit : Node {
    std::vector<ExprPtr> elements;

    Kind kind() const override { return Kind::VectorLit; }
};

struct VarRef : Node {
    std::string name;

    Kind kind() const override { return Kind::VarRef; }
};

struct BinaryOp : Node {
    BinOpKind op;
    ExprPtr left;
    ExprPtr right;

    Kind kind() const override { return Kind::BinaryOp; }
};

struct UnaryOp : Node {
    UnaryOpKind op;
    ExprPtr operand;

    Kind kind() const override { return Kind::UnaryOp; }
};

struct FuncCall : Node {
    std::string name;
    std::vector<Argument> args;

    Kind kind() const override { return Kind::FuncCall; }
};

struct MethodCall : Node {
    ExprPtr object;
    std::string method;
    std::vector<Argument> args;

    Kind kind() const override { return Kind::MethodCall; }
};

struct MemberAccess : Node {
    ExprPtr object;
    std::string member;

    Kind kind() const override { return Kind::MemberAccess; }
};

struct TernaryExpr : Node {
    ExprPtr condition;
    ExprPtr thenExpr;
    ExprPtr elseExpr;

    Kind kind() const override { return Kind::TernaryExpr; }
};

struct SelectorExpr : Node {
    std::string selectorType;       // "faces", "edges", "vertices"
    std::vector<SelectorArg> args;

    Kind kind() const override { return Kind::SelectorExpr; }
};

struct SketchExpr : Node {
    std::vector<Argument> initArgs;             // arguments to sketch(...)
    std::vector<std::pair<std::string, std::vector<Argument>>> commands;
        // Each command is (name, args), e.g. ("line_to", [...])

    Kind kind() const override { return Kind::SketchExpr; }
};

// =====================================================================
//  Helper: safe downcast
// =====================================================================

template <typename T>
T* as(Node* node) {
    return (node && node->kind() == T{}.kind()) ? static_cast<T*>(node) : nullptr;
}

template <typename T>
const T* as(const Node* node) {
    return (node && node->kind() == T{}.kind()) ? static_cast<const T*>(node) : nullptr;
}

} // namespace ast
} // namespace opendcad
```

### Node Hierarchy Summary

```
Node (abstract base, carries SourceLoc)
 |
 +-- Program                 { body: [StmtPtr] }
 |
 +-- Definitions
 |    +-- FnDef              { name, params: [ParamDef], body: [StmtPtr] }
 |    +-- ModuleDef          { name, params: [ParamDef], body: [StmtPtr] }
 |    +-- ProcessDef         { name, fields: [(string, ExprPtr)] }
 |
 +-- Statements
 |    +-- ImportStmt         { importKind, path, alias, names }
 |    +-- ConstDecl          { name, value: ExprPtr }
 |    +-- LetDecl            { name, value: ExprPtr }
 |    +-- Assignment         { name, value: ExprPtr }
 |    +-- ForStmt            { varName, rangeStart, rangeEnd, iterable, body }
 |    +-- IfStmt             { conditions, bodies (parallel vectors) }
 |    +-- ReturnStmt         { value: ExprPtr (optional) }
 |    +-- ExportStmt         { value: ExprPtr, name }
 |    +-- ExprStmt           { expr: ExprPtr }
 |    +-- DfmCheckStmt       { part: ExprPtr, processName }
 |
 +-- Expressions
      +-- NumberLit          { value: double, unit: Unit }
      +-- StringLit          { value: string }
      +-- BoolLit            { value: bool }
      +-- VectorLit          { elements: [ExprPtr] }
      +-- VarRef             { name }
      +-- BinaryOp           { op, left, right }
      +-- UnaryOp            { op, operand }
      +-- FuncCall           { name, args: [Argument] }
      +-- MethodCall         { object, method, args: [Argument] }
      +-- MemberAccess       { object, member }
      +-- TernaryExpr        { condition, thenExpr, elseExpr }
      +-- SelectorExpr       { selectorType, args: [SelectorArg] }
      +-- SketchExpr         { initArgs, commands: [(name, [Argument])] }
```

---

## 3. ASTBuilder (`src/parser/ASTBuilder.h`)

The ASTBuilder is an ANTLR visitor that walks the concrete parse tree exactly once and produces the AST. It does no evaluation -- only structural transformation and source location copying.

### Class Declaration

```cpp
#pragma once

#include "OpenDCADBaseVisitor.h"
#include "AST.h"
#include <string>

namespace opendcad {

class ASTBuilder : public OpenDCAD::OpenDCADBaseVisitor {
public:
    explicit ASTBuilder(const std::string& filename = "");

    // Entry point: returns a Program node
    std::unique_ptr<ast::Program> build(
        OpenDCAD::OpenDCADParser::ProgramContext* ctx);

    // ---- Top-level ----
    antlrcpp::Any visitProgram(
        OpenDCAD::OpenDCADParser::ProgramContext* ctx) override;

    // ---- Import ----
    antlrcpp::Any visitImportPlain(
        OpenDCAD::OpenDCADParser::ImportPlainContext* ctx) override;
    antlrcpp::Any visitImportAlias(
        OpenDCAD::OpenDCADParser::ImportAliasContext* ctx) override;
    antlrcpp::Any visitImportDestructure(
        OpenDCAD::OpenDCADParser::ImportDestructureContext* ctx) override;
    antlrcpp::Any visitImportStd(
        OpenDCAD::OpenDCADParser::ImportStdContext* ctx) override;

    // ---- Definitions ----
    antlrcpp::Any visitFnDef(
        OpenDCAD::OpenDCADParser::FnDefContext* ctx) override;
    antlrcpp::Any visitModuleDef(
        OpenDCAD::OpenDCADParser::ModuleDefContext* ctx) override;
    antlrcpp::Any visitProcessDef(
        OpenDCAD::OpenDCADParser::ProcessDefContext* ctx) override;

    // ---- Statements ----
    antlrcpp::Any visitLetStmt(
        OpenDCAD::OpenDCADParser::LetStmtContext* ctx) override;
    antlrcpp::Any visitConstStmt(
        OpenDCAD::OpenDCADParser::ConstStmtContext* ctx) override;
    antlrcpp::Any visitAssignStmt(
        OpenDCAD::OpenDCADParser::AssignStmtContext* ctx) override;
    antlrcpp::Any visitExportStmt(
        OpenDCAD::OpenDCADParser::ExportStmtContext* ctx) override;
    antlrcpp::Any visitReturnStmt(
        OpenDCAD::OpenDCADParser::ReturnStmtContext* ctx) override;
    antlrcpp::Any visitDfmCheckStmt(
        OpenDCAD::OpenDCADParser::DfmCheckStmtContext* ctx) override;
    antlrcpp::Any visitExprStmt(
        OpenDCAD::OpenDCADParser::ExprStmtContext* ctx) override;
    antlrcpp::Any visitForStmt(
        OpenDCAD::OpenDCADParser::ForStmtContext* ctx) override;
    antlrcpp::Any visitIfStmt(
        OpenDCAD::OpenDCADParser::IfStmtContext* ctx) override;

    // ---- For iterable variants ----
    antlrcpp::Any visitRangeIterable(
        OpenDCAD::OpenDCADParser::RangeIterableContext* ctx) override;
    antlrcpp::Any visitExprIterable(
        OpenDCAD::OpenDCADParser::ExprIterableContext* ctx) override;

    // ---- Expressions ----
    antlrcpp::Any visitTernaryExpr(
        OpenDCAD::OpenDCADParser::TernaryExprContext* ctx) override;
    antlrcpp::Any visitLogicOr(
        OpenDCAD::OpenDCADParser::LogicOrContext* ctx) override;
    antlrcpp::Any visitLogicAnd(
        OpenDCAD::OpenDCADParser::LogicAndContext* ctx) override;
    antlrcpp::Any visitLogicNot(
        OpenDCAD::OpenDCADParser::LogicNotContext* ctx) override;
    antlrcpp::Any visitComparison(
        OpenDCAD::OpenDCADParser::ComparisonContext* ctx) override;
    antlrcpp::Any visitAddSub(
        OpenDCAD::OpenDCADParser::AddSubContext* ctx) override;
    antlrcpp::Any visitMulDivMod(
        OpenDCAD::OpenDCADParser::MulDivModContext* ctx) override;
    antlrcpp::Any visitUnaryNeg(
        OpenDCAD::OpenDCADParser::UnaryNegContext* ctx) override;
    antlrcpp::Any visitMethodCallExpr(
        OpenDCAD::OpenDCADParser::MethodCallExprContext* ctx) override;
    antlrcpp::Any visitMemberAccess(
        OpenDCAD::OpenDCADParser::MemberAccessContext* ctx) override;
    antlrcpp::Any visitFuncCallExpr(
        OpenDCAD::OpenDCADParser::FuncCallExprContext* ctx) override;

    // ---- Primary expressions ----
    antlrcpp::Any visitNumberLit(
        OpenDCAD::OpenDCADParser::NumberLitContext* ctx) override;
    antlrcpp::Any visitStringLit(
        OpenDCAD::OpenDCADParser::StringLitContext* ctx) override;
    antlrcpp::Any visitBoolTrue(
        OpenDCAD::OpenDCADParser::BoolTrueContext* ctx) override;
    antlrcpp::Any visitBoolFalse(
        OpenDCAD::OpenDCADParser::BoolFalseContext* ctx) override;
    antlrcpp::Any visitVecLit(
        OpenDCAD::OpenDCADParser::VecLitContext* ctx) override;
    antlrcpp::Any visitVarRef(
        OpenDCAD::OpenDCADParser::VarRefContext* ctx) override;
    antlrcpp::Any visitParenExpr(
        OpenDCAD::OpenDCADParser::ParenExprContext* ctx) override;
    antlrcpp::Any visitSelectorPrimary(
        OpenDCAD::OpenDCADParser::SelectorPrimaryContext* ctx) override;
    antlrcpp::Any visitSketchPrimary(
        OpenDCAD::OpenDCADParser::SketchPrimaryContext* ctx) override;

    // ---- Arguments ----
    antlrcpp::Any visitNamedArg(
        OpenDCAD::OpenDCADParser::NamedArgContext* ctx) override;
    antlrcpp::Any visitPositionalArg(
        OpenDCAD::OpenDCADParser::PositionalArgContext* ctx) override;

private:
    std::string filename_;

    // Extract SourceLoc from any parser context
    SourceLoc locFrom(antlr4::ParserRuleContext* ctx) const;

    // Extract string content from STRING token (strip quotes, unescape)
    std::string unescapeString(const std::string& raw) const;

    // Parse unit suffix text to Unit enum
    ast::Unit parseUnit(const std::string& text) const;

    // Build argument list from argList context
    std::vector<ast::Argument> buildArgList(
        OpenDCAD::OpenDCADParser::ArgListContext* ctx);

    // Build parameter list from paramList context
    std::vector<ast::ParamDef> buildParamList(
        OpenDCAD::OpenDCADParser::ParamListContext* ctx);

    // Build statement list from block context
    std::vector<ast::StmtPtr> buildBlock(
        OpenDCAD::OpenDCADParser::BlockContext* ctx);

    // Unwrap antlrcpp::Any to ExprPtr
    ast::ExprPtr toExpr(antlrcpp::Any result);
};

} // namespace opendcad
```

### Key Implementation Details

**Source location extraction:**
```cpp
SourceLoc ASTBuilder::locFrom(antlr4::ParserRuleContext* ctx) const {
    auto* tok = ctx->getStart();
    return SourceLoc{
        filename_,
        tok ? tok->getLine() : 0,
        tok ? tok->getCharPositionInLine() : 0
    };
}
```

**String unescaping:**
```cpp
std::string ASTBuilder::unescapeString(const std::string& raw) const {
    // raw includes surrounding quotes: "hello\nworld"
    std::string result;
    // Skip first and last character (the quotes)
    for (size_t i = 1; i < raw.size() - 1; ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size() - 1) {
            ++i;
            switch (raw[i]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                default: result += '\\'; result += raw[i]; break;
            }
        } else {
            result += raw[i];
        }
    }
    return result;
}
```

**Unit parsing:**
```cpp
ast::Unit ASTBuilder::parseUnit(const std::string& text) const {
    if (text == "mm")   return ast::Unit::Mm;
    if (text == "cm")   return ast::Unit::Cm;
    if (text == "m")    return ast::Unit::M;
    if (text == "in")   return ast::Unit::In;
    if (text == "ft")   return ast::Unit::Ft;
    if (text == "thou") return ast::Unit::Thou;
    if (text == "deg")  return ast::Unit::Deg;
    if (text == "rad")  return ast::Unit::Rad;
    return ast::Unit::None;
}
```

**Visitor pattern for expressions.** Each visitor method creates the appropriate AST node, copies the source location, recursively visits child expressions, and returns the node wrapped in `antlrcpp::Any`. Example for binary operations:

```cpp
antlrcpp::Any ASTBuilder::visitAddSub(
    OpenDCAD::OpenDCADParser::AddSubContext* ctx)
{
    auto node = std::make_unique<ast::BinaryOp>();
    node->loc = locFrom(ctx);
    node->left = toExpr(visit(ctx->expr(0)));
    node->right = toExpr(visit(ctx->expr(1)));

    std::string opText = ctx->children[1]->getText();
    node->op = (opText == "+") ? ast::BinOpKind::Add : ast::BinOpKind::Sub;

    return static_cast<ast::ExprPtr>(std::move(node));
}
```

---

## 4. Evaluator Expansion

### Updated Value Type System

Expand `ValueType` in `src/parser/Value.h` to cover all Phase 4 types:

```cpp
enum class ValueType {
    NUMBER,     // double, with optional Unit
    STRING,     // std::string
    BOOL,       // bool
    VECTOR,     // std::vector<double>
    SHAPE,      // ShapePtr
    SELECTOR,   // Selector result (set of faces/edges/vertices)
    FUNCTION,   // User-defined function closure
    NIL,        // no value
};
```

**Number with unit.** The `Value` class gains an optional `Unit` field:

```cpp
class Value {
    // ... existing fields ...
    ast::Unit unit_ = ast::Unit::None;

public:
    static ValuePtr makeNumber(double v, ast::Unit unit = ast::Unit::None);
    ast::Unit unit() const;

    // Convert value to base units (mm for length, rad for angle)
    double toBaseUnits() const;
};
```

Unit conversion factors (all lengths to mm, all angles to radians):

| Unit | Factor |
|------|--------|
| `mm` | 1.0 |
| `cm` | 10.0 |
| `m` | 1000.0 |
| `in` | 25.4 |
| `ft` | 304.8 |
| `thou` | 0.0254 |
| `deg` | PI/180 |
| `rad` | 1.0 |

**Function closure value:**

```cpp
struct FunctionValue {
    std::string name;                       // function name (for error messages)
    std::vector<ast::ParamDef> params;      // parameter definitions (names + defaults)
    std::vector<ast::StmtPtr>* body;        // pointer to AST body (owned by AST)
    EnvironmentPtr closure;                 // captured environment at definition time
};
```

The `Value` class stores `FunctionValue` via `std::shared_ptr<FunctionValue>`:

```cpp
static ValuePtr makeFunction(std::shared_ptr<FunctionValue> fn);
const FunctionValue& asFunction() const;
```

**Selector value:**

```cpp
struct SelectorValue {
    std::string type;       // "faces", "edges", "vertices"
    // Resolved set of topological entities from the target shape
    // Stored as indices or TopoDS sub-shapes
    std::vector<TopoDS_Shape> selected;
};
```

### Updated Environment

```cpp
class Environment {
public:
    explicit Environment(EnvironmentPtr parent = nullptr);

    // Phase 0: define and lookup
    void define(const std::string& name, ValuePtr value);
    void defineConst(const std::string& name, ValuePtr value);
    ValuePtr lookup(const std::string& name) const;
    bool has(const std::string& name) const;

    // Phase 4 additions:

    // Assignment (reassign existing mutable binding, searching parent chain)
    // Throws EvalError if name not found or if name is const
    void assign(const std::string& name, ValuePtr value);

    // Check if a binding is const
    bool isConst(const std::string& name) const;

    // Create child scope
    EnvironmentPtr createChild();

    // Export management (for import resolution)
    void addExport(const std::string& name, ValuePtr value);
    const std::unordered_map<std::string, ValuePtr>& exports() const;

    EnvironmentPtr parent() const;

private:
    EnvironmentPtr parent_;
    std::unordered_map<std::string, ValuePtr> bindings_;
    std::unordered_set<std::string> constNames_;    // track which bindings are const
    std::unordered_map<std::string, ValuePtr> exports_;
};
```

### Evaluator Class (Updated)

```cpp
#pragma once

#include "AST.h"
#include "Value.h"
#include "Environment.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace opendcad {

struct ExportEntry {
    std::string name;
    ShapePtr shape;
};

// Return value sentinel for early return from functions
struct ReturnSignal {
    ValuePtr value;
};

class Evaluator {
public:
    Evaluator();

    // Main entry point: evaluate a Program AST
    void evaluate(const ast::Program& program, const std::string& filename = "");

    // Results
    const std::vector<ExportEntry>& exports() const;

    // Import resolution callback
    using ImportResolver = std::function<EnvironmentPtr(
        const std::string& path,        // import path
        const std::string& currentFile   // file containing the import
    )>;
    void setImportResolver(ImportResolver resolver);

private:
    EnvironmentPtr globalEnv_;
    std::vector<ExportEntry> exports_;
    std::string filename_;
    ImportResolver importResolver_;

    // ---- Statement evaluation ----
    void evalStmt(const ast::Node& stmt);
    void evalLetDecl(const ast::LetDecl& node);
    void evalConstDecl(const ast::ConstDecl& node);
    void evalAssignment(const ast::Assignment& node);
    void evalExportStmt(const ast::ExportStmt& node);
    void evalReturnStmt(const ast::ReturnStmt& node);
    void evalForStmt(const ast::ForStmt& node);
    void evalIfStmt(const ast::IfStmt& node);
    void evalExprStmt(const ast::ExprStmt& node);
    void evalDfmCheckStmt(const ast::DfmCheckStmt& node);
    void evalImportStmt(const ast::ImportStmt& node);
    void evalFnDef(const ast::FnDef& node);
    void evalModuleDef(const ast::ModuleDef& node);
    void evalProcessDef(const ast::ProcessDef& node);

    // ---- Expression evaluation ----
    ValuePtr evalExpr(const ast::Node& expr);
    ValuePtr evalNumberLit(const ast::NumberLit& node);
    ValuePtr evalStringLit(const ast::StringLit& node);
    ValuePtr evalBoolLit(const ast::BoolLit& node);
    ValuePtr evalVectorLit(const ast::VectorLit& node);
    ValuePtr evalVarRef(const ast::VarRef& node);
    ValuePtr evalBinaryOp(const ast::BinaryOp& node);
    ValuePtr evalUnaryOp(const ast::UnaryOp& node);
    ValuePtr evalFuncCall(const ast::FuncCall& node);
    ValuePtr evalMethodCall(const ast::MethodCall& node);
    ValuePtr evalMemberAccess(const ast::MemberAccess& node);
    ValuePtr evalTernaryExpr(const ast::TernaryExpr& node);
    ValuePtr evalSelectorExpr(const ast::SelectorExpr& node);
    ValuePtr evalSketchExpr(const ast::SketchExpr& node);

    // ---- Function/module call machinery ----

    // Call a user-defined function or module
    ValuePtr callFunction(const FunctionValue& fn,
                          const std::vector<ast::Argument>& args,
                          const SourceLoc& callSite);

    // Resolve arguments: match positional + named args to params, fill defaults
    std::vector<ValuePtr> resolveArgs(
        const std::vector<ast::ParamDef>& params,
        const std::vector<ast::Argument>& args,
        const SourceLoc& callSite);

    // ---- Import machinery ----
    EnvironmentPtr resolveImport(const std::string& path);

    // ---- Unit checking ----
    void checkUnitCompatibility(ValuePtr left, ValuePtr right,
                                ast::BinOpKind op,
                                const SourceLoc& loc);

    // ---- Error helpers ----
    [[noreturn]] void error(const std::string& msg, const SourceLoc& loc);

    // ---- Current environment (changes during function calls) ----
    EnvironmentPtr currentEnv_;
};

} // namespace opendcad
```

### Evaluator Method Specifications

#### Function/Module Calls

When `evalFuncCall` encounters a function name:

1. Look up the name in the current environment.
2. If the value is a `FUNCTION`, call `callFunction()`.
3. If the name is in `ShapeRegistry::instance().hasFactory()`, call the factory.
4. Otherwise, check built-in functions (`range`, `len`, `abs`, `sin`, `cos`, `sqrt`, `print`, etc.).
5. If nothing matches, throw `EvalError`.

```
callFunction(fn, args, callSite):
    resolvedArgs = resolveArgs(fn.params, args, callSite)
    childEnv = fn.closure->createChild()
    for i in range(fn.params.size()):
        childEnv->define(fn.params[i].name, resolvedArgs[i])
    savedEnv = currentEnv_
    currentEnv_ = childEnv
    try:
        for stmt in *fn.body:
            evalStmt(stmt)
    catch ReturnSignal& ret:
        currentEnv_ = savedEnv
        return ret.value
    currentEnv_ = savedEnv
    return Value::makeNil()
```

#### Argument Resolution

```
resolveArgs(params, args, callSite):
    result = vector of size params.size(), all nullptr
    positionalIndex = 0

    // First pass: place all positional args
    for arg in args:
        if arg is positional:
            if positionalIndex >= params.size():
                error("too many arguments", callSite)
            result[positionalIndex] = evalExpr(*arg.value)
            positionalIndex++

    // Second pass: place all named args
    for arg in args:
        if arg is named:
            find paramIndex where params[paramIndex].name == arg.name
            if not found:
                error("unknown parameter '" + arg.name + "'", callSite)
            if result[paramIndex] is not nullptr:
                error("parameter '" + arg.name + "' already specified", callSite)
            result[paramIndex] = evalExpr(*arg.value)

    // Third pass: fill defaults for unspecified params
    for i in range(params.size()):
        if result[i] is nullptr:
            if params[i].defaultValue:
                result[i] = evalExpr(*params[i].defaultValue)
            else:
                error("missing argument for parameter '" + params[i].name + "'",
                      callSite)

    return result
```

#### For-Loop Evaluation

```
evalForStmt(node):
    if node.isRange():
        startVal = evalExpr(*node.rangeStart)->asNumber()
        endVal = evalExpr(*node.rangeEnd)->asNumber()
        for i in startVal ..< endVal:
            childEnv = currentEnv_->createChild()
            childEnv->define(node.varName, Value::makeNumber(i))
            savedEnv = currentEnv_
            currentEnv_ = childEnv
            for stmt in node.body:
                evalStmt(stmt)
            currentEnv_ = savedEnv
    else:
        collection = evalExpr(*node.iterable)
        if collection is VECTOR:
            for element in collection->asVector():
                // same child-env pattern as above
        else:
            error("cannot iterate over " + collection->typeName(), node.loc)
```

#### If/Else Evaluation

```
evalIfStmt(node):
    for i in range(node.conditions.size()):
        condVal = evalExpr(*node.conditions[i])
        if condVal->isTruthy():
            for stmt in node.bodies[i]:
                evalStmt(stmt)
            return
    if node.hasElse():
        for stmt in node.bodies.back():
            evalStmt(stmt)
```

#### Import Resolution

```
evalImportStmt(node):
    if node.importKind == Std:
        // Load from built-in standard library
        importedEnv = loadStdModule(node.path)
    else:
        // Resolve relative path against current file
        importedEnv = importResolver_(node.path, filename_)

    switch node.importKind:
        case Plain:
            // Import all exports into current env
            for (name, value) in importedEnv->exports():
                currentEnv_->define(name, value)
        case Alias:
            // Create a namespace value or import all under prefix
            for (name, value) in importedEnv->exports():
                currentEnv_->define(node.alias + "." + name, value)
        case Destructure:
            for name in node.names:
                value = importedEnv->exports()[name]
                if not found: error("'" + name + "' not exported", node.loc)
                currentEnv_->define(name, value)
```

#### Unit Compatibility

Arithmetic operations check unit compatibility:

| Operation | Rule |
|-----------|------|
| `+`, `-` | Both operands must have the same unit category (both length or both angle). Result inherits the unit. Mixed units within a category convert to base (mm or rad). |
| `*` | `unit * unitless` = unit, `unitless * unit` = unit, `unit * unit` = error |
| `/` | `unit / unitless` = unit, `unit / unit` = unitless (dimensionless ratio), `unitless / unit` = error |
| Comparison | Same rules as `+` / `-`: compatible categories only |

```
checkUnitCompatibility(left, right, op, loc):
    lu = left->unit()
    ru = right->unit()
    if lu == None and ru == None: return  // both unitless, always ok
    if op is Add or Sub or comparison:
        if unitCategory(lu) != unitCategory(ru):
            error("cannot " + opName(op) + " " + unitName(lu) + " and " +
                  unitName(ru), loc)
    if op is Mul:
        if lu != None and ru != None:
            error("cannot multiply " + unitName(lu) + " by " + unitName(ru), loc)
    if op is Div:
        // unit/unit is fine (produces unitless), unitless/unit is error
        if lu == None and ru != None:
            error("cannot divide unitless by " + unitName(ru), loc)
```

#### Error Messages

All errors include source location in the format:

```
filename:line:col: error: message
```

Example:

```
battery.dcad:5:15: error: cannot call 'fillet' on type 'number'
utils.dcad:12:3: error: unknown parameter 'raidus' for function 'cylinder'
battery.dcad:8:1: error: cannot add 'mm' and 'deg'
```

The `error()` helper:

```cpp
[[noreturn]] void Evaluator::error(const std::string& msg, const SourceLoc& loc) {
    throw EvalError(msg, loc);
}
```

---

## 5. Hot-Loading Architecture

### Overview

The viewer currently loads a single STL file at startup and renders it statically. Phase 4 adds the ability to watch `.dcad` source files for changes and automatically re-parse, re-evaluate, and re-tessellate, updating the 3D view in real time. This is the core workflow enabler for interactive CAD design.

### FileWatcher (`src/core/FileWatcher.h` / `.cpp`)

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <chrono>
#include <functional>

namespace opendcad {

class FileWatcher {
public:
    // Poll interval (default 500ms)
    explicit FileWatcher(
        std::chrono::milliseconds interval = std::chrono::milliseconds(500));

    // Add a file to watch
    void watch(const std::string& path);

    // Add multiple files (e.g., all imported files)
    void watchAll(const std::vector<std::string>& paths);

    // Remove all watches and re-add just these paths
    void replaceWatchList(const std::vector<std::string>& paths);

    // Clear all watches
    void clear();

    // Check for changes. Returns true if any watched file has been modified
    // since the last successful poll. Non-blocking.
    bool poll();

    // Get the list of files that changed on the last poll() that returned true
    const std::vector<std::string>& changedFiles() const;

    // Callback-based API (alternative to polling)
    using ChangeCallback = std::function<void(const std::vector<std::string>&)>;
    void setCallback(ChangeCallback cb);

private:
    struct WatchEntry {
        std::string path;
        std::filesystem::file_time_type lastModified;
        bool exists = false;
    };

    std::chrono::milliseconds interval_;
    std::chrono::steady_clock::time_point lastPollTime_;
    std::unordered_map<std::string, WatchEntry> entries_;
    std::vector<std::string> changedFiles_;
    ChangeCallback callback_;

    // Read current modification time, handling errors gracefully
    static std::filesystem::file_time_type getModTime(const std::string& path);
};

} // namespace opendcad
```

### Implementation Notes

**Stat-based polling.** The initial implementation uses `std::filesystem::last_write_time()` with a 500ms polling interval. This is simple, portable, and sufficient for interactive use. The poll is cheap (one `stat` syscall per watched file), so it can run every frame in the render loop with no noticeable overhead.

```cpp
bool FileWatcher::poll() {
    auto now = std::chrono::steady_clock::now();
    if (now - lastPollTime_ < interval_) return false;
    lastPollTime_ = now;

    changedFiles_.clear();
    for (auto& [path, entry] : entries_) {
        auto currentModTime = getModTime(path);
        bool currentExists = std::filesystem::exists(path);

        if (currentExists != entry.exists || currentModTime != entry.lastModified) {
            changedFiles_.push_back(path);
            entry.lastModified = currentModTime;
            entry.exists = currentExists;
        }
    }

    if (!changedFiles_.empty() && callback_) {
        callback_(changedFiles_);
    }

    return !changedFiles_.empty();
}
```

**Future upgrade path.** On Linux, replace polling with `inotify` via `inotify_init1(IN_NONBLOCK)` + `inotify_add_watch()` for instant notification. On macOS, use `kqueue` / `FSEvents`. The `FileWatcher` interface stays the same -- only the implementation changes. The `poll()` method would check the inotify/kqueue file descriptor for ready events instead of stat-ing files.

### Rebuild Pipeline in Viewer

The viewer's render loop gains a rebuild step:

```
// In viewer main(), before render loop:
FileWatcher watcher(500ms);
watcher.watch(inputDcadPath);

// State for hot-loading:
std::unique_ptr<ast::Program> currentAST;
std::vector<ShapePtr> currentShapes;
std::vector<float> currentVBO;
bool hasError = false;
std::string errorMessage;
std::vector<std::string> importedFiles;  // for watching imported files

// In render loop:
while (!glfwWindowShouldClose(win)) {
    // --- Hot-load check ---
    if (watcher.poll()) {
        try {
            // 1. Re-parse
            std::string src = loadFile(inputDcadPath);
            ANTLRInputStream input(src);
            OpenDCADLexer lexer(&input);
            CommonTokenStream tokens(&lexer);
            OpenDCADParser parser(&tokens);
            auto* tree = parser.program();

            if (parser.getNumberOfSyntaxErrors() > 0) {
                throw EvalError("syntax error in " + inputDcadPath, {});
            }

            // 2. Build AST
            ASTBuilder builder(inputDcadPath);
            auto ast = builder.build(tree);

            // 3. Evaluate
            Evaluator evaluator;
            evaluator.setImportResolver(importResolverFn);
            evaluator.evaluate(*ast, inputDcadPath);

            // 4. Collect exported shapes
            currentShapes.clear();
            for (const auto& exp : evaluator.exports()) {
                currentShapes.push_back(exp.shape);
            }

            // 5. Tessellate and rebuild VBO
            rebuildVBO(currentShapes, currentVBO, vbo);

            // 6. Update bounding box
            recomputeBounds(currentVBO, g_minB, g_maxB);

            // 7. Update import watch list
            importedFiles = evaluator.importedFiles();
            watcher.replaceWatchList({inputDcadPath});
            watcher.watchAll(importedFiles);

            // 8. Clear error state
            hasError = false;
            errorMessage.clear();

        } catch (const EvalError& e) {
            hasError = true;
            errorMessage = e.what();
            // Keep previous geometry -- do NOT clear currentVBO
        } catch (const GeometryError& e) {
            hasError = true;
            errorMessage = std::string("geometry: ") + e.what();
        }
    }

    // --- Render ---
    // ... existing render code, using currentVBO ...

    // --- Error overlay ---
    if (hasError) {
        drawErrorOverlay(errorMessage);
        // Optionally render previous geometry with gray tint
    }

    glfwSwapBuffers(win);
    glfwPollEvents();
}
```

### Tessellation for Hot-Loading

```cpp
void rebuildVBO(const std::vector<ShapePtr>& shapes,
                std::vector<float>& interleaved,
                GLuint vbo)
{
    interleaved.clear();

    for (const auto& shape : shapes) {
        // Heal
        TopoDS_Shape healed = ShapeHealer::heal(shape->getShape());

        // Mesh with moderate deflection for interactive use
        BRepMesh_IncrementalMesh mesher(healed, 0.05, false, 0.5, true);
        mesher.Perform();
        if (!mesher.IsDone()) continue;

        // Extract triangles
        for (TopExp_Explorer f(healed, TopAbs_FACE); f.More(); f.Next()) {
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri =
                BRep_Tool::Triangulation(TopoDS::Face(f.Current()), loc);
            if (tri.IsNull()) continue;

            for (int i = 1; i <= tri->NbTriangles(); ++i) {
                int i1, i2, i3;
                tri->Triangle(i).Get(i1, i2, i3);

                gp_Pnt p1 = tri->Node(i1).Transformed(loc);
                gp_Pnt p2 = tri->Node(i2).Transformed(loc);
                gp_Pnt p3 = tri->Node(i3).Transformed(loc);

                // Compute face normal
                gp_Vec n = gp_Vec(p1, p2).Crossed(gp_Vec(p1, p3));
                if (n.SquareMagnitude() > 0) n.Normalize();

                for (const gp_Pnt& p : {p1, p2, p3}) {
                    interleaved.push_back((float)p.X());
                    interleaved.push_back((float)p.Y());
                    interleaved.push_back((float)p.Z());
                    interleaved.push_back((float)n.X());
                    interleaved.push_back((float)n.Y());
                    interleaved.push_back((float)n.Z());
                }
            }
        }
    }

    // Upload to GPU
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 interleaved.size() * sizeof(float),
                 interleaved.data(),
                 GL_DYNAMIC_DRAW);  // DYNAMIC since we update frequently
}
```

### Error Resilience

**Principle: never lose the user's view.** If a rebuild fails (syntax error, evaluation error, geometry error), the viewer keeps the previous frame's geometry on screen and overlays an error message.

**Visual error feedback:**

```cpp
void drawErrorOverlay(const std::string& message) {
    // Semi-transparent red bar at the top of the viewport
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    int w, h;
    // ... get viewport size ...

    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    // Red background bar
    float barHeight = 60.0f;
    glColor4f(0.8f, 0.1f, 0.1f, 0.85f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f(w, 0);
    glVertex2f(w, barHeight);
    glVertex2f(0, barHeight);
    glEnd();

    // Error text
    glColor3f(1.0f, 1.0f, 1.0f);
    draw_text_screen(16.0f, 20.0f, message.c_str(), 2.0f);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glPopAttrib();
}
```

**Previous geometry graying.** When `hasError` is true, modify the fragment shader's base color uniform to a desaturated gray (e.g., multiply by 0.3) to visually indicate that the displayed geometry is stale.

### Import Chain Watching

When the Evaluator processes an `import` statement, it records the resolved file path. After evaluation, the viewer retrieves all imported file paths and adds them to the FileWatcher:

```cpp
// In Evaluator:
std::vector<std::string> importedFiles_;

EnvironmentPtr Evaluator::resolveImport(const std::string& path) {
    std::string resolvedPath = resolveRelativePath(path, filename_);
    importedFiles_.push_back(resolvedPath);
    // ... parse and evaluate the imported file ...
}

const std::vector<std::string>& Evaluator::importedFiles() const {
    return importedFiles_;
}
```

This means that editing an imported utility file triggers a rebuild just as editing the main file would. The watch list is refreshed on every successful rebuild, so newly added imports are automatically watched.

### Full Rebuild Strategy

Phase 4 uses a full rebuild strategy: every change triggers a complete re-parse and re-evaluate of the entire file tree. This is the simplest correct approach.

**Why not incremental?** Incremental evaluation would require tracking which AST nodes depend on which variables, and only re-evaluating the changed subgraph. This is complex to implement correctly (especially with imports, closures, and geometry boolean operations that depend on multiple shapes). The full rebuild time for typical CAD scripts (hundreds of lines, dozens of shapes) is expected to be under 500ms, which is acceptable for interactive use.

**Future optimization path (Phase 6+):** If rebuild times grow beyond 1 second for complex models:
1. Cache imported module environments (invalidate when the imported file changes).
2. Cache intermediate geometry results (hash-based keying on AST subtree + argument values).
3. Parallelize independent shape constructions.

---

## 6. Implementation Order

### Step 1: Grammar Update

**File:** `grammar/OpenDCAD.g4`

Replace the existing grammar with the complete grammar from Section 1. Regenerate the ANTLR parser:

```bash
rm -rf build/generated/antlr/
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Verify that the generated files compile. Write a small test script exercising each new construct and confirm it parses without errors (using only the parser, not the Evaluator).

**Estimated effort:** 2-3 hours.

### Step 2: AST Nodes

**Files:** `src/parser/AST.h`

Create all AST node types as defined in Section 2. This is a header-only file with no implementation (all nodes are plain data structs with a `kind()` method).

**Estimated effort:** 1-2 hours.

### Step 3: ASTBuilder

**Files:** `src/parser/ASTBuilder.h`, `src/parser/ASTBuilder.cpp`

Implement the ANTLR visitor that converts parse tree to AST. Test by parsing example scripts, building the AST, and printing a debug dump of the tree structure.

**Dependencies:** Step 1 (grammar), Step 2 (AST nodes).

**Estimated effort:** 4-6 hours.

### Step 4: Environment Updates

**Files:** `src/parser/Environment.h`, `src/parser/Environment.cpp`

Add `assign()`, `isConst()`, `defineConst()`, `createChild()`, `addExport()`, and `exports()` methods. Update tests.

**Dependencies:** None (extends existing Environment).

**Estimated effort:** 1-2 hours.

### Step 5: Evaluator Core (let, const, arithmetic, functions)

**Files:** `src/parser/Evaluator.h`, `src/parser/Evaluator.cpp`, `src/parser/Value.h`, `src/parser/Value.cpp`

Rewrite the Evaluator to walk the AST instead of the ANTLR parse tree. Implement:
- `evalLetDecl`, `evalConstDecl`, `evalAssignment`
- All arithmetic (`evalBinaryOp`, `evalUnaryOp`) with unit checking
- `evalFuncCall` with argument resolution
- `evalFnDef`, `evalModuleDef` (closure creation)
- `callFunction` with child environment, parameter binding, ReturnSignal
- `evalForStmt`, `evalIfStmt`, `evalReturnStmt`
- `evalTernaryExpr`
- Comparison and logical operators in `evalBinaryOp`

Add `FUNCTION` type to `Value` with `FunctionValue` struct. Add unit-aware number creation and `toBaseUnits()`.

**Dependencies:** Steps 2-4.

**Estimated effort:** 8-12 hours.

### Step 6: Built-in Shape Dispatch

**Files:** `src/geometry/ShapeRegistry.h`, `src/geometry/ShapeRegistry.cpp`

Update ShapeRegistry to support named arguments. The existing factory/method signature uses `std::vector<ValuePtr>` for positional args. Add an overload or adapter that accepts `std::vector<ast::Argument>`:

```cpp
ShapePtr callFactory(const std::string& name,
                     const std::vector<ValuePtr>& args,
                     const std::vector<std::string>& names = {}) const;
```

The Evaluator resolves named arguments before calling the registry, so this may require minimal changes to ShapeRegistry itself. Ensure all existing factories and methods work with the new Evaluator.

**Dependencies:** Step 5.

**Estimated effort:** 2-3 hours.

### Step 7: Wire into main()

**Files:** `src/main.cpp`

Update the main entry point:
1. Parse with ANTLR.
2. Build AST with `ASTBuilder`.
3. Evaluate with the new `Evaluator`.
4. Export results.

```cpp
int main(int argc, char** argv) {
    writeLogo();
    ShapeRegistry::instance().registerDefaults();

    std::string inputFile = (argc > 1) ? argv[1] : "examples/battery.dcad";
    std::string outputDir = (argc > 2) ? argv[2] : "build/bin";

    std::string src = loadFile(inputFile);

    // Parse
    ANTLRInputStream input(src);
    OpenDCADLexer lexer(&input);
    CommonTokenStream tokens(&lexer);
    OpenDCADParser parser(&tokens);
    auto* tree = parser.program();

    if (parser.getNumberOfSyntaxErrors() > 0) {
        std::cerr << "Syntax errors found\n";
        return 1;
    }

    // Build AST
    ASTBuilder builder(inputFile);
    auto ast = builder.build(tree);

    // Evaluate
    Evaluator evaluator;
    evaluator.setImportResolver(/* file-based resolver */);
    try {
        evaluator.evaluate(*ast, inputFile);
    } catch (const EvalError& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    // Export
    for (const auto& exp : evaluator.exports()) {
        TopoDS_Shape healed = ShapeHealer::heal(exp.shape->getShape());
        StepExporter::write(healed, outputDir + "/" + exp.name + ".step");
        StlExporter::write(healed, outputDir + "/" + exp.name + ".stl", {0.01});
    }

    return 0;
}
```

**Dependencies:** Steps 1-6.

**Estimated effort:** 1-2 hours.

### Step 8: Selector Evaluation

**Files:** `src/parser/Evaluator.cpp` (evalSelectorExpr), `src/parser/Value.h` (SELECTOR type)

Implement `@faces(...)`, `@edges(...)`, `@vertices(...)` selector resolution. This requires:
1. The selector operates on a shape (typically accessed via method chaining: `part.@faces(top)`).
2. Resolve the selector type (faces, edges, vertices) using `TopExp_Explorer`.
3. Filter by semantic name (top = max Z normal, bottom = min Z normal, etc.) or by property comparison (radius > 2mm).
4. Return a `SelectorValue` containing the matched topological entities.
5. Wire selectors into operations like `fillet(@edges(top), 2mm)` where the selector replaces an "all edges" default.

**Dependencies:** Steps 5-7.

**Estimated effort:** 4-6 hours.

### Step 9: FileWatcher

**Files:** `src/core/FileWatcher.h`, `src/core/FileWatcher.cpp`

Implement the FileWatcher class as described in Section 5. Unit test with a simple program that watches a temp file, modifies it, and verifies `poll()` returns true.

**Dependencies:** None (independent of parser/evaluator work).

**Estimated effort:** 2-3 hours.

### Step 10: Hot-Load Integration in Viewer

**Files:** `src/viewer/viewer.cpp`

Modify the viewer to:
1. Accept a `.dcad` file path instead of (or in addition to) an STL path.
2. Create a `FileWatcher` for the input file.
3. On change detection, run the full parse-evaluate-tessellate pipeline.
4. Update the VBO with new geometry.
5. Display errors without losing the previous view.
6. Watch imported files.

**Dependencies:** Steps 7-9.

**Estimated effort:** 4-6 hours.

### Step 11: DFM Analyzer

**Files:** `src/dfm/DfmAnalyzer.h`, `src/dfm/DfmAnalyzer.cpp`

Implement `dfm_check` evaluation:
1. Look up the `@process` definition by name.
2. Extract process parameters (layer height, nozzle diameter, etc.).
3. Analyze the shape against manufacturing constraints.
4. Report warnings (thin walls, overhangs, small features, etc.).

This is the most domain-specific step and can be implemented incrementally, starting with simple checks (minimum wall thickness, minimum hole diameter) and adding more sophisticated analysis later.

**Dependencies:** Steps 5-7.

**Estimated effort:** 6-10 hours (basic checks).

---

## 7. Files to Create

| File | Lines (est.) | Purpose |
|------|-------------|---------|
| `src/parser/AST.h` | 350 | All AST node types |
| `src/parser/ASTBuilder.h` | 120 | ASTBuilder class declaration |
| `src/parser/ASTBuilder.cpp` | 500 | Parse tree to AST conversion |
| `src/core/FileWatcher.h` | 50 | File change detection header |
| `src/core/FileWatcher.cpp` | 80 | stat-based polling implementation |
| `src/dfm/DfmAnalyzer.h` | 40 | DFM analysis header |
| `src/dfm/DfmAnalyzer.cpp` | 200 | DFM constraint checking |

## Files to Modify

| File | Changes |
|------|---------|
| `grammar/OpenDCAD.g4` | Complete rewrite (Section 1) |
| `src/parser/Value.h` | Add FUNCTION, SELECTOR types; unit-aware numbers |
| `src/parser/Value.cpp` | Implement unit conversion, function/selector accessors |
| `src/parser/Environment.h` | Add assign, defineConst, isConst, createChild, exports |
| `src/parser/Environment.cpp` | Implement new methods |
| `src/parser/Evaluator.h` | Rewrite to walk AST; add function call machinery |
| `src/parser/Evaluator.cpp` | Complete rewrite for AST-based evaluation |
| `src/geometry/ShapeRegistry.h` | Minor: named arg support |
| `src/geometry/ShapeRegistry.cpp` | Minor: named arg adapter |
| `src/main.cpp` | Wire ASTBuilder into pipeline |
| `src/viewer/viewer.cpp` | Add hot-loading, FileWatcher, error overlay |
| `CMakeLists.txt` | Add new source files |

---

## 8. CMakeLists.txt Updates

### New source files

```cmake
add_executable(opendcad
  src/main.cpp
  src/core/output.cpp
  src/core/Timer.cpp
  src/core/FileWatcher.cpp
  src/geometry/Shape.cpp
  src/geometry/ShapeRegistry.cpp
  src/parser/Value.cpp
  src/parser/Environment.cpp
  src/parser/Evaluator.cpp
  src/parser/ASTBuilder.cpp
  src/export/StepExporter.cpp
  src/export/StlExporter.cpp
  src/export/ShapeHealer.cpp
  src/dfm/DfmAnalyzer.cpp
)
```

### Viewer executable

```cmake
add_executable(opendcad_viewer
  src/viewer/viewer.cpp
  src/core/output.cpp
  src/core/Timer.cpp
  src/core/FileWatcher.cpp
  src/geometry/Shape.cpp
  src/geometry/ShapeRegistry.cpp
  src/parser/Value.cpp
  src/parser/Environment.cpp
  src/parser/Evaluator.cpp
  src/parser/ASTBuilder.cpp
  src/export/ShapeHealer.cpp
)
```

### Include directories

```cmake
target_include_directories(opendcad PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/geometry
  ${CMAKE_SOURCE_DIR}/src/parser
  ${CMAKE_SOURCE_DIR}/src/export
  ${CMAKE_SOURCE_DIR}/src/dfm
  ${GEN_DIR}
)
```

---

## 9. Verification

### Test Scripts

Create test scripts exercising each new language feature:

**`examples/test_functions.dcad`:**
```
fn rounded_box(w, d, h, r = 1.0) {
    return box(w, d, h).fillet(r);
}

let b = rounded_box(50mm, 30mm, 10mm);
let b2 = rounded_box(w: 50mm, d: 30mm, h: 10mm, r: 2mm);
export b as default_fillet;
export b2 as custom_fillet;
```

**`examples/test_loops.dcad`:**
```
let base = box(100mm, 100mm, 5mm);
let result = base;

for i in 0..4 {
    let x = -30 + i * 20;
    let hole = cylinder(3mm, 10mm).translate(x, 0, 0);
    result = result.cut(hole);
}

export result as drilled_plate;
```

**`examples/test_conditionals.dcad`:**
```
const USE_CHAMFER = false;
let part = box(40mm, 40mm, 20mm);

if USE_CHAMFER {
    part = part.chamfer(2mm);
} else {
    part = part.fillet(2mm);
}

export part as conditional_part;
```

**`examples/test_imports.dcad`:**
```
import "./utils.dcad" as utils;

let base = utils.create_base(80mm, 120mm);
export base as imported_base;
```

**`examples/test_selectors.dcad`:**
```
let part = box(40mm, 30mm, 20mm);
let filleted = part.fillet(@edges(top), 2mm);
export filleted as selective_fillet;
```

### Acceptance Criteria

1. All existing Phase 0 scripts (`examples/battery.dcad`) produce identical geometry output.
2. New test scripts parse, evaluate, and export without errors.
3. The viewer loads a `.dcad` file directly, renders the geometry, and updates within 1 second of a file save.
4. Syntax errors in the source file display a red error overlay without crashing the viewer.
5. Editing an imported file triggers a rebuild in the viewer.
6. Named arguments work: `cylinder(r: 10mm, h: 20mm)` produces the same result as `cylinder(10mm, 20mm)`.
7. Unit suffixed numbers are converted to base units (mm, rad) before passing to geometry operations.
8. `const` bindings cannot be reassigned (throws error with source location).
9. Function closures capture their defining environment correctly.

---

## 10. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Grammar ambiguity with unit suffixes (`10in` where `in` is also a keyword for `for x in ...`) | Medium | Medium | The `in` keyword appears in `for IDENT 'in'` context. As a unit suffix, it follows a NUMBER token. ANTLR resolves this positionally -- `for` requires `IDENT 'in'`, and `NUMBER unitSuffix?` tries to match a following IDENT against known units. If ambiguity persists, rename the unit to `inch`. |
| `antlrcpp::Any` type erasure bugs when passing AST nodes through visitor returns | Medium | High | Standardize all visitor returns as `ast::ExprPtr` (via `std::unique_ptr<Node>`). Use the `toExpr()` helper consistently. Add runtime checks in debug builds. |
| Hot-loading race condition (file write not yet flushed when watcher triggers) | Low | Medium | Use a debounce: after detecting a change, wait an additional 100ms before reading the file, to allow the editor to finish writing. If parse fails with unexpected EOF, silently retry on next poll. |
| Import cycles (A imports B, B imports A) | Medium | High | Track files currently being imported in a set. If an import cycle is detected, throw `EvalError("circular import detected: A -> B -> A")`. |
| Performance: full rebuild too slow for complex models | Low | Medium | Acceptable for Phase 4 (full rebuild). Profile if users report >1s rebuild times. Add caching in Phase 6+. |
| ANTLR C++ runtime memory management conflicts with unique_ptr AST nodes | Medium | Medium | ANTLR visitor methods return `antlrcpp::Any`, which uses `std::any` internally. Wrapping `unique_ptr` in `Any` requires careful move semantics. Test thoroughly. If issues arise, use `shared_ptr` for AST nodes instead. |
| Breaking backward compatibility with Phase 0 grammar | Low | Low | The new grammar is a strict superset of Phase 0. All Phase 0 scripts remain valid. Run `battery.dcad` as a regression test. |
