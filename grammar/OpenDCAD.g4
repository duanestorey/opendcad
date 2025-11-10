grammar OpenDCAD;

program : (expr ';')* EOF ;
expr    : IDENT '(' argList? ')' ('.' IDENT '(' argList? ')')* ;
argList : arg (',' arg)* ;
arg     : (IDENT ':')? NUMBER ;

IDENT   : [A-Za-z_][A-Za-z0-9_]* ;
NUMBER  : [0-9]+ ('.' [0-9]+)? ;
WS      : [ \t\r\n]+ -> skip ;
