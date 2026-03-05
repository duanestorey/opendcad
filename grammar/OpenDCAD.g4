grammar OpenDCAD;

// ========== Parser Rules ==========

// ---------- Program ----------

program
  : stmt* EOF
  ;

stmt
  : letStmt ';'
  | constStmt ';'
  | assignStmt ';'
  | compoundAssignStmt ';'
  | postfixIncrStmt ';'
  | exportStmt ';'
  | exprStmt ';'
  | ifStmt
  | forStmt
  | whileStmt
  | fnDecl
  | importStmt ';'
  | returnStmt ';'
  ;

// ---------- Declarations ----------

// let x = expr;
letStmt
  : LET IDENT '=' expr
  ;

// const PI = 3.14159;
constStmt
  : CONST IDENT '=' expr
  ;

// x = expr;  OR  x[i] = expr;
assignStmt
  : IDENT '=' expr                         # assignVar
  | IDENT '[' expr ']' '=' expr           # assignIndex
  ;

// x += expr; etc.
compoundAssignStmt
  : IDENT (PLUS_EQ | MINUS_EQ | STAR_EQ | SLASH_EQ) expr
  ;

// i++; or i--;
postfixIncrStmt
  : IDENT (INCR | DECR)
  ;

// export <expr> as <id>;
exportStmt
  : EXPORT expr AS IDENT
  ;

// import "file.dcad";
importStmt
  : IMPORT STRING
  ;

// return expr;
returnStmt
  : RETURN expr?
  ;

// expression as statement (for side-effect calls like fn())
exprStmt
  : chainExpr
  ;

// ---------- Control Flow ----------

block
  : '{' stmt* '}'
  ;

ifStmt
  : IF '(' expr ')' block
    (ELSE IF '(' expr ')' block)*
    (ELSE block)?
  ;

forStmt
  : FOR '(' forInit ';' expr ';' forUpdate ')' block   # forCStyle
  | FOR '(' IDENT IN expr ')' block                     # forEach
  ;

forInit
  : LET IDENT '=' expr                    # forInitLet
  | IDENT '=' expr                         # forInitAssign
  ;

forUpdate
  : IDENT '=' expr                                       # forUpdateAssign
  | IDENT (PLUS_EQ | MINUS_EQ | STAR_EQ | SLASH_EQ) expr  # forUpdateCompound
  | IDENT (INCR | DECR)                                   # forUpdateIncr
  ;

whileStmt
  : WHILE '(' expr ')' block
  ;

// ---------- Functions ----------

fnDecl
  : FN IDENT '(' paramList? ')' block
  ;

paramList
  : param (',' param)*
  ;

param
  : IDENT ('=' expr)?
  ;

// ---------- Chains & Calls ----------

chainExpr
  : postfix
  ;

// Either a ctor call (with args) optionally followed by methods,
// OR a variable followed by at least one method.
// A bare IDENT is *not* a chain.
postfix
  : c=call ('.' methodCall)*                 # postFromCall
  | var=IDENT '.' methodCall ('.' methodCall)*  # postFromVar
  ;

call
  : IDENT '(' argList? ')'
  ;

methodCall
  : IDENT '(' argList? ')'
  ;

argList
  : arg (',' arg)*
  ;

arg
  : IDENT '=' expr        # namedArg
  | expr                   # positionalArg
  ;

// ---------- Expressions ----------

expr
  : expr OR expr                               # logicalOr
  | expr AND expr                              # logicalAnd
  | expr (EQ | NEQ) expr                       # equality
  | expr ('<' | '>' | LTE | GTE) expr         # comparison
  | expr ('+' | '-') expr                      # addSub
  | expr ('*' | '/' | '%') expr               # mulDivMod
  | '!' expr                                   # logicalNot
  | '-' expr                                   # unaryNeg
  | expr '[' expr ']'                          # indexAccess
  | primary                                    # primaryExpr
  ;

primary
  : NUMBER
  | STRING
  | TRUE
  | FALSE
  | NIL_LIT
  | vectorLiteral
  | listLiteral
  | postfix
  | IDENT
  | '(' expr ')'
  ;

// [x,y] or [x,y,z]  (legacy vector syntax, kept for backward compat)
// ANTLR4 tries this before listLiteral for 2-3 element brackets
vectorLiteral
  : '[' expr ',' expr (',' expr)? ']'
  ;

// List literal: [] or [a] or [a,b,c,d,...] (4+ elements, or single element)
listLiteral
  : '[' ']'
  | '[' expr (',' expr)* ']'
  ;

// ========== Lexer Rules ==========

// Multi-character operators (must be before single-char implicit tokens)
EQ       : '==';
NEQ      : '!=';
LTE      : '<=';
GTE      : '>=';
AND      : '&&';
OR       : '||';
PLUS_EQ  : '+=';
MINUS_EQ : '-=';
STAR_EQ  : '*=';
SLASH_EQ : '/=';
INCR     : '++';
DECR     : '--';

// Keywords (must be before IDENT)
LET    : 'let';
CONST  : 'const';
EXPORT : 'export';
AS     : 'as';
IF     : 'if';
ELSE   : 'else';
FOR    : 'for';
WHILE  : 'while';
IN     : 'in';
FN     : 'fn';
RETURN : 'return';
IMPORT : 'import';
TRUE   : 'true';
FALSE  : 'false';
NIL_LIT: 'nil';

STRING
  : '"' (~["\\\r\n] | '\\' .)* '"'
  ;

IDENT
  : [a-zA-Z_] [a-zA-Z_0-9]*
  ;

NUMBER
  : [0-9]+ ('.' [0-9]+)? ( [eE] [+-]? [0-9]+ )?
  | '.' [0-9]+ ( [eE] [+-]? [0-9]+ )?
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
