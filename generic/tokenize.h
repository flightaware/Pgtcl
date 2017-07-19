/*
** tokenize.h
**
** An tokenizer for SQL
*/

enum sqltoken {
TK_BITAND, TK_BITNOT, TK_BITOR, TK_BLOB, TK_COMMA, TK_CONCAT, TK_DOT, TK_EQ, TK_FLOAT, TK_GE,
TK_GT, TK_ID, TK_ILLEGAL, TK_INTEGER, TK_LE, TK_LP, TK_LSHIFT, TK_LT, TK_MINUS, TK_NE, TK_PLUS,
TK_REM, TK_RP, TK_RSHIFT, TK_SEMI, TK_SLASH, TK_SPACE, TK_STAR, TK_STRING, TK_TCLVAR, TK_SQLVAR
};

int Pg_sqlite3GetToken(const unsigned char *z, enum sqltoken *tokenType);
