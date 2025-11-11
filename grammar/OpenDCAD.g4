grammar OpenDCAD;

// ---------- Program ----------

program
  : stmt* EOF
  ;

stmt
  : letStmt ';'
  | exportStmt ';'
  ;

// let a = b;                    (alias)
// let a = bin(...).fillet(...); (shape chain)
// let a = [0,22,0]; or 21/2;    (param value)
letStmt
  : 'let' IDENT '=' IDENT          # letAlias
  | 'let' IDENT '=' chainExpr      # letChain
  | 'let' IDENT '=' expr           # letValue
  ;

// export <expr> as <id>;
exportStmt
  : 'export' expr 'as' IDENT
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
  : IDENT '(' argList ')'                        // ctors must have args
  ;

methodCall
  : IDENT '(' argList ')'                        // methods must have args
  ;

argList
  : expr (',' expr)*                             // one or more via call/method rules
  ;

// ---------- Expressions ----------

expr
  : expr ('*' | '/' | '%') expr         # mulDivMod
  | expr ('+' | '-') expr               # addSub
  | '-' expr                            # unaryNeg
  | primary                             # primaryExpr
  ;

primary
  : NUMBER
  | vectorLiteral
  | postfix
  | IDENT            // <-- add this: bare variable as an expression
  | '(' expr ')'
  ;

// [x,y] or [x,y,z]
vectorLiteral
  : '[' expr ',' expr (',' expr)? ']'
  ;

// ---------- Lexer ----------

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