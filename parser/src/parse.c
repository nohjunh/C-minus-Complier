#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
parser 구현은 scanner와 함께 각 헤더파일, C파일을 나누지 않고 하나의 parse.c에서 처리함.
단, 후에 구현의 편리함을 주석으로 경계구분.

int EchoSource = FALSE; 를 해서 parsing 과정만 보이게 함.
만약, Source도 같이 출력하고 싶다면, TRUE로 변경해주면 됨.
*/

////////////////////////////////////////////////// GLOBALS.H 헤더파일 ///////////////////////////////////////////
#define FALSE 0
#define TRUE 1

/* MAXRESERVED = the number of reserved words */
#define MAXRESERVED 6 // else, if, int, return, void, while

typedef enum {
    /* book-keeping tokens */
    ENDFILE, ERROR,
    /* reserved words */
    ELSE, IF, INT, RETURN, VOID, WHILE,
    /* multicharacter tokens */
    ID, NUM,
    /* Special symbols */
    /*TIMES='*', OVER='/', LT='<', LTE='<=', GTE='>=', NE='Not equal !=',
      ASSIGN= '=', LPAREN='(', RPAREN=')', LBRACK='[', LBRACE='{'
      COMMENT는 '/*' 와 같이 OVER와 TIMES의 concatenation형태인데
      이고 따로 토큰화해서 fprintf할 필요가 없으므로 state만 INCOMMENT로 처리*/
      PLUS, MINUS, TIMES, OVER, LT, LTE, GT, GTE, EQ, NE,
      ASSIGN, SEMI, COMMA,
      LPAREN, RPAREN, LBRACK, RBRACK, LBRACE, RBRACE,
      STOP_BEFORE_END
} TokenType;

FILE* listing; /* listing output text file */
int lineno = 0; /* source line number for listing */
FILE* source; /* source code text file */

/**************************************************/
/***********   Syntax tree for parsing ************/
/**************************************************/

typedef enum { StmtK, ExpK } NodeKind;
typedef enum { compound_stmtK, selection_stmtK, iteration_stmtK, return_stmtK, callK } StmtKind;
typedef enum { checkArrayVarK, checkVarK, fun_declarationK, OpK, ConstK, IdK, AssignK } ExpKind;

/* ExpType is used for type checking */
typedef enum { Void, Integer } ExpType;

#define MAXCHILDREN 3

typedef struct treeNode
{
    struct treeNode* child[MAXCHILDREN];
    struct treeNode* sibling;
    int lineno;
    NodeKind nodekind;
    union { StmtKind stmt; ExpKind exp; } kind;
    union {
        TokenType op;
        int val;
        char* name;
    } attr;
    ExpType type; /* for type checking of exps */
    // include_param이 0이면 인자X
    // 1이면 인자로 쓰임.
    int include_param;
    // C에서는 배열을 인자로 넘겨줄때 size를 같이 넘겨줘야 함.
    int array_size;
} TreeNode;


/* EchoSource = TRUE causes the source program to
 * be echoed to the listing file with line numbers
 * during parsing
 */
int EchoSource = FALSE;


/* TraceScan = TRUE causes token information to be
 * printed to the listing file as each token is
 * recognized by the scanner
 */
int TraceScan = FALSE;

/* TraceParse = TRUE causes the syntax tree to be
 * printed to the listing file in linearized form
 * (using indents for children)
 */
int TraceParse = TRUE;


/* Error = TRUE prevents further passes if an error occurs */
int Error = FALSE;

////////////////////////////////////////////////// UTIL.C 파일 ///////////////////////////////////////////

/* Procedure printToken prints a token
* and its lexeme to the listing file
*/
void printToken(TokenType token, const char* tokenString)
{
    switch (token)
    {
    case ELSE:
    case IF:
    case INT:
    case RETURN:
    case VOID:
    case WHILE:

        fprintf(listing,
            "reserved word: %s\n", tokenString);
        break;
    case PLUS: fprintf(listing, "+\n"); break;
    case MINUS: fprintf(listing, "-\n"); break;
    case TIMES: fprintf(listing, "*\n"); break;
    case OVER: fprintf(listing, "/\n"); break;
    case LT: fprintf(listing, "<\n"); break;
    case LTE: fprintf(listing, "<=\n"); break;
    case GT: fprintf(listing, ">\n"); break;
    case GTE: fprintf(listing, ">=\n"); break;
    case EQ: fprintf(listing, "==\n"); break;
    case NE: fprintf(listing, "!=\n"); break;
    case ASSIGN: fprintf(listing, "=\n"); break; // TINY에서는 ':=' 이거지만, 여기서는 '='로 ASSIGN표현
    case SEMI: fprintf(listing, ";\n"); break;
    case COMMA: fprintf(listing, ",\n"); break;
    case LPAREN: fprintf(listing, "(\n"); break;
    case RPAREN: fprintf(listing, ")\n"); break;
    case LBRACK: fprintf(listing, "[\n"); break;
    case RBRACK: fprintf(listing, "]\n"); break;
    case LBRACE: fprintf(listing, "{\n"); break;
    case RBRACE: fprintf(listing, "}\n"); break;
    case ENDFILE: fprintf(listing, "EOF\n"); break;
    case STOP_BEFORE_END: fprintf(listing, "stop before ending\n"); break;
    case NUM:
        fprintf(listing,
            "NUM, val= %s\n", tokenString);
        break;
    case ID:
        fprintf(listing,
            "ID, name= %s\n", tokenString);
        break;
    case ERROR:
        fprintf(listing,
            "Error: %s\n", tokenString); // 에러토큰의 경우 "Error: 해당 error string"
        break;
    default: /* should never happen */
        fprintf(listing, "Unknown token: %d\n", token);
    }
}

/* Function newStmtNode creates a new statement
 * node for syntax tree construction
 */
TreeNode* newStmtNode(StmtKind kind)
{
    TreeNode* t = (TreeNode*)malloc(sizeof(TreeNode));
    if (t == NULL)
        fprintf(listing, "Out of memory error at line %d\n", lineno);
    else {
        for (int i = 0; i < MAXCHILDREN; i++) t->child[i] = NULL;
        t->sibling = NULL;
        t->nodekind = StmtK;
        t->kind.stmt = kind;
        t->lineno = lineno;
    }
    return t;
}

/* Function newExpNode creates a new expression
 * node for syntax tree construction
 */
TreeNode* newExpNode(ExpKind kind)
{
    TreeNode* t = (TreeNode*)malloc(sizeof(TreeNode));
    if (t == NULL)
        fprintf(listing, "Out of memory error at line %d\n", lineno);
    else {
        for (int i = 0; i < MAXCHILDREN; i++) t->child[i] = NULL;
        t->sibling = NULL;
        t->nodekind = ExpK;
        t->kind.exp = kind;
        t->lineno = lineno;
        t->type = Void;
        t->include_param = 0;
    }
    return t;
}

/* Function copyString allocates and makes a new
 * copy of an existing string
 */
char* copyString(char* s)
{
    if (s == NULL) return NULL;
    int n = strlen(s) + 1;
    char* t = malloc(n);
    if (t == NULL)
        fprintf(listing, "Out of memory error at line %d\n", lineno);
    else strcpy(t, s);
    return t;
}

/* Variable indentno is used by printTree to
 * store current number of spaces to indent
 */
static int indentno = 0;

/* macros to increase/decrease indentation */
#define INDENT indentno+=2
#define UNINDENT indentno-=2

/* printSpaces indents by printing spaces */
static void printSpaces(void)
{
    for (int i = 0; i < indentno; i++) {
        fprintf(listing, " ");
    }
}

/* procedure printTree prints a syntax tree to the
 * listing file using indentation to indicate subtrees
 */
void printTree(TreeNode* tree)
{
    int i;
    INDENT;
    while (tree != NULL) {
        printSpaces();
        if (tree->nodekind == StmtK)
        {
            switch (tree->kind.stmt) {
            case compound_stmtK:
                fprintf(listing, "Compound-stmt :\n");
                break;
            case selection_stmtK:
                if (tree->child[2] == NULL) {
                    fprintf(listing, "If-stmt (else 미포함) :\n");
                }
                else {
                    fprintf(listing, "If-stmt (else 포함) :\n");
                }
                break;
            case iteration_stmtK:
                fprintf(listing, "While-stmt (iteration) :\n");
                break;
            case return_stmtK:
                if (tree->child[0] == NULL) {
                    fprintf(listing, "Return; \n");
                }
                else {
                    fprintf(listing, "Return-stmt :\n");
                }
                break;
            case callK:
                if (tree->child[0] == NULL) {
                    fprintf(listing, "Call-stmt : %s \n", tree->attr.name);
                }
                else {
                    fprintf(listing, "Call-stmt : %s \n", tree->attr.name);
                }
                break;
            default:
                fprintf(listing, "Unknown ExpNode kind\n");
                break;
            }
        }
        else if (tree->nodekind == ExpK)
        {
            switch (tree->kind.exp) {
            case checkArrayVarK:
                if (tree->include_param == 1) {
                    fprintf(listing, "[ Parameter in Array => name : %s (%s) ] \n", tree->attr.name, ((tree->type == Integer) ? "int" : "void"));
                }
                else {
                    fprintf(listing, "[ Declaration of Array => name : %s (%s), (array_size : %d) ]\n", tree->attr.name, ((tree->type == Integer) ? "int" : "void"), tree->array_size);
                }
                break;
            case checkVarK:
                if (tree->include_param == 1) {
                    fprintf(listing, "[ Parameter variable => name : %s (%s) ]\n", tree->attr.name, ((tree->type == Integer) ? "int" : "void"));
                }
                else {
                    fprintf(listing, "[ Declaration of variable => name : %s (%s) ]\n", tree->attr.name, ((tree->type == Integer) ? "int" : "void"));
                }
                break;
            case fun_declarationK:
                fprintf(listing, "[ func-declaration => name : %s (%s) ]\n", tree->attr.name, ((tree->type == Integer) ? "int" : "void"));
                break;
            case OpK:
                fprintf(listing, "Op : ");
                printToken(tree->attr.op, "\0");
                break;
            case ConstK:
                fprintf(listing, "Const: %d\n", tree->attr.val);
                break;
            case IdK:
                fprintf(listing, "Id : %s\n", tree->attr.name);
                break;
            case AssignK:
                fprintf(listing, "Assign : (좌=우) \n");
                break;
            default:
                fprintf(listing, "Unknown ExpNode kind\n");
                break;
            }
        }
        else fprintf(listing, "Unknown node kind\n");
        for (i = 0; i < MAXCHILDREN; i++)
            printTree(tree->child[i]);
        tree = tree->sibling;
    }
    UNINDENT;
}

////////////////////////////////////////////////// SCAN.H 헤더 파일 ///////////////////////////////////////////
/* MAXTOKENLEN is the maximum size of a token */
#define MAXTOKENLEN 40

/* tokenString array stores the lexeme of each token */
char tokenString[MAXTOKENLEN + 1]; // SCAN.C파일에도 들어감



////////////////////////////////////////////////// SCAN.C 파일 ///////////////////////////////////////////

typedef enum {
    // 기존 TINY참고자료에 비해 추가되는 state는 2개의 연산자가 겹쳐 표현되는 것들
    // 즉, ==, <=, >=, !=, /*, */ 을 순서대로 IN_ASSIGN, IN_LT, IN_GT, IN_NE, IN_OVER로 state표현
    START, IN_ASSIGN, IN_COMMENT, IN_NUM, IN_ID, DONE, IN_LT, IN_GT, IN_NE, IN_OVER
}StateType;


/* BUFLEN = length of the input buffer for
   source code lines */
#define BUFLEN 256

static char lineBuf[BUFLEN]; /* holds the current line */
static int linepos = 0; /* current position in LineBuf */
static int bufsize = 0; /* current size of buffer string */
static int EOF_flag = FALSE; /* corrects ungetNextChar behavior on EOF */
int paramcheck = 0;
int RPARENcheck = 0;

/* getNextChar fetches the next non-blank character
   from lineBuf, reading in a new line if lineBuf is
   exhausted */
static int getNextChar(void)
{
    if (!(linepos < bufsize))
    {
        lineno++;
        if (fgets(lineBuf, BUFLEN - 1, source)) {
            if (EchoSource) fprintf(listing, "%4d: %s", lineno, lineBuf);
            bufsize = strlen(lineBuf);
            linepos = 0;
            return lineBuf[linepos++]; // ++후위연산
        }
        else {
            EOF_flag = TRUE;
            return EOF;
        }
    }
    else
        return lineBuf[linepos++];
}

/* ungetNextChar backtracks one character in lineBuf */
static void ungetNextChar(void) {
    if (!EOF_flag) linepos--;
}

/* lookup table of reserved words */
static struct {
    char* str;
    TokenType tok;
}
reservedWords[MAXRESERVED] // 지정해주신 keyworkds는 6개
= { {"else",ELSE} ,{"if",IF}, {"int",INT},
    {"return",RETURN}, {"void",VOID}, {"while", WHILE}
};

/* lookup an identifier to see if it is a reserved word */
/* uses linear search */
static TokenType reservedLookup(char* s) {
    int i;
    for (i = 0; i < MAXRESERVED; i++)
        if (!strcmp(s, reservedWords[i].str))
            return reservedWords[i].tok;
    return ID;
}


/****************************************/
/* the primary function of the scanner  */
/****************************************/
/* function getToken returns the next token in source file */

TokenType getToken()
{  /* index for storing into tokenString */
    int tokenStringIndex = 0;
    /* holds current token to be returned */
    TokenType currentToken; //리턴되어질 현재토큰
    /* current state - always begins at START */
    StateType state = START;
    /* flag to indicate save to tokenString */
    int save;
    while (state != DONE)  // state가 DONE이 아니라면 계속 돌아감
    {
        int c = getNextChar();
        save = TRUE;
        switch (state) {
        case START:
            if (isdigit(c))
                state = IN_NUM;
            else if (isalpha(c))
                state = IN_ID;

            /* White space consists of blanks, newlines, and tabs
               White space is ignored except that it must separate ID’s, NUM’s, and
               keywords.*/
            else if ((c == ' ') || (c == '\t') || (c == '\n')) {
                save = FALSE;
            }
            else if (c == '=') {
                save = FALSE;
                state = IN_ASSIGN;
            }
            else if (c == '<') {
                save = FALSE;
                state = IN_LT;
            }
            else if (c == '>') {
                save = FALSE;
                state = IN_GT;
            }
            else if (c == '!') {
                save = FALSE;
                state = IN_NE;
            }
            else if (c == '/') {
                save = FALSE;
                state = IN_OVER;
            }
            else {
                state = DONE;
                switch (c) {
                case EOF:
                    save = FALSE;
                    currentToken = ENDFILE;
                    break;
                case '+':
                    currentToken = PLUS;
                    break;
                case '-':
                    currentToken = MINUS;
                    break;
                case '*':
                    currentToken = TIMES;
                    break;
                case '(':
                    currentToken = LPAREN;
                    break;
                case ')':
                    currentToken = RPAREN;
                    break;
                case '{':
                    currentToken = LBRACE;
                    break;
                case '}':
                    currentToken = RBRACE;
                    break;
                case '[':
                    currentToken = LBRACK;
                    break;
                case ']':
                    currentToken = RBRACK;
                    break;
                case ';':
                    currentToken = SEMI;
                    break;
                case ',':
                    currentToken = COMMA;
                    break;
                default:
                    currentToken = ERROR;
                    break;
                }
            }
            break;

        case IN_ASSIGN: // ASSIGN으로 시작하는데 equal인지 assign인지 구분
            state = DONE;
            if (c == '=') // '==' 즉, eqaul이므로 TOKEN은 EQ
                currentToken = EQ;
            else { // '=' 하나이므로 ASSIGN 토큰
                currentToken = ASSIGN;
                ungetNextChar();
            }
            break;
        case IN_COMMENT:
            save = FALSE;
            if (c == '*') {
                c = getNextChar();
                if (c == '/') { // '*/' 로 Comment 닫음.
                    state = START;
                }
                else if (c == EOF) { // IN_COMMENT안에서  '*'에서 '/' 없이 파일을 다 읽었다면
                    state = DONE;
                    currentToken = STOP_BEFORE_END;
                }
                /* backup in the input */
                else { //버퍼위치를 다시 직전으로 되돌려 그 값으로 처리할 수 있게 함.
                    ungetNextChar();
                }
            }
            else if (c == EOF) { // IN_COMMENT 상태에서 EOF가 되면,
                state = DONE;
                currentToken = STOP_BEFORE_END;
            }
            break;

        case IN_NUM:
            if (!isdigit(c)) { /* backup in the input */
                ungetNextChar();
                save = FALSE;
                state = DONE;
                currentToken = NUM;
            }
            break;

        case IN_ID:                             /* backup in the input */
            if (!(isalpha(c))) { //  IN_ID state에서는 letter letter* 을 받는다.
                ungetNextChar();
                save = FALSE;
                state = DONE;
                currentToken = ID;
            }
            break;

        case IN_LT: // LT로 시작하는데 '<='인지 '<' 인지 구분
            state = DONE;
            if (c == '=') // '<='
                currentToken = LTE;
            else { // '<'
                currentToken = LT;
                ungetNextChar();
            }
            break;

        case IN_GT: // GT로 시작하는데 '>='인지 '>' 인지 구분
            state = DONE;
            if (c == '=') // '>='
                currentToken = GTE;
            else { // '>'
                currentToken = GT;
                ungetNextChar();
            }
            break;

        case IN_NE: // !로 시작하는데 '!='가 아니라면 ERROR
            state = DONE;
            if (c == '=') // NOT EQUAL(!=)
                currentToken = NE;
            else if (c == '/') {
                c = lineBuf[linepos - 2]; // c= '!'
                currentToken = ERROR;
                ungetNextChar();
            }
            else { // ERROR
                currentToken = ERROR;
                ungetNextChar();
            }
            break;

            /*comments cannot be placed within tokens) and may include than one
            line. Comments may not be nested.*/
        case IN_OVER: // OVER로 시작하는데 Comment인지 나누기인지 구분
            if (c == '*') { // comment left '/*'
                state = IN_COMMENT;
                save = FALSE;
            }
            else { // '*'이 안나왔다면 OVER만, 즉 '/'를 표현 -> 나누기 기호
                ungetNextChar();
                state = DONE;
                currentToken = OVER;
            }
            break;

        case DONE:
        default: /* should never happen */
            fprintf(listing, "Scanner Bug: state= %d\n", state);
            state = DONE;
            currentToken = ERROR;
            break;
        }
        if ((save) && (tokenStringIndex <= MAXTOKENLEN))
            tokenString[tokenStringIndex++] = (char)c;
        if (state == DONE) {
            tokenString[tokenStringIndex] = '\0';
            if (currentToken == ID)
                currentToken = reservedLookup(tokenString);
        }
    }
    if (TraceScan) {
        fprintf(listing, "\t%d: ", lineno);
        printToken(currentToken, tokenString);
    }
    return currentToken;
} /* end getToken */


////////////////////////////////////////////////// PARSE.C 파일 ///////////////////////////////////////////


static TokenType token; /* holds current token */

/* function prototypes for recursive calls */

// Syntax and Semantics of C-

static TreeNode* declaration_list(void);
static TreeNode* declaration(void);
static TreeNode* var_declaration(void);
static ExpType type_specifier(void); // type을 표현해야 함. 따라서, 자료형 ExpType로!   // 반환할 토큰은 int or void
static TreeNode* fun_declaration(void);
static TreeNode* params(void);
static TreeNode* param_list(ExpType type);
static TreeNode* param(ExpType type);
static TreeNode* compound_stmt(void);
static TreeNode* local_declarations(void);
static TreeNode* statement_list(void);
static TreeNode* statement(void);
static TreeNode* expression_stmt(void);
static TreeNode* selection_stmt(void);
static TreeNode* iteration_stmt(void);
static TreeNode* return_stmt(void);
static TreeNode* expression(void);
static TreeNode* simple_expression(TreeNode* g);
static TreeNode* additive_expression(TreeNode* g);
static TreeNode* term(TreeNode* g);
static TreeNode* factor(TreeNode* g);
static TreeNode* call(void);
static TreeNode* args(void);
static TreeNode* arg_list(void);
static TreeNode* parse1(void);

static void syntaxError(char* message)
{
    fprintf(listing, "\n>>> ");
    fprintf(listing, "Syntax error at line %d: %s", lineno, message);
    Error = TRUE;
}

static void match(TokenType expected)
{
    if (token == RPAREN && RPARENcheck == 1) {
        fprintf(listing, "\n-- 해당 에러 구문 recovery :: 이어서 syntax tree 구성시작 --\n");
        Error = FALSE;
        RPARENcheck = 0;
        token = getToken();
        TreeNode* syntaxTree = parse1();
        if (TraceParse) {
            fprintf(listing, "\n***** \npossibility that it is a grammatical error in the main function. \n");
            fprintf(listing, "Error handling for this is an exception and has not been processed yet. Check code again \n***** \n");
            fprintf(listing, "\n Syntax tree:\n");
            printTree(syntaxTree);
        }
        fclose(source);
        fclose(listing);
        exit(1);
    }

    if (token == expected) token = getToken();
    else {
        syntaxError("unexpected token (match함수) -> ");
        printToken(token, tokenString);
        if (strcmp(tokenString, ")") == 0) {
            paramcheck = 0;
        }
        fprintf(listing, "      ");
    }
}

TreeNode* declaration_list(void)
{
    TreeNode* t = declaration();
    TreeNode* p = t;
    while (token != ENDFILE)
    {
        TreeNode* q;
        // TINY에서는 stmt-sequence; statement l statement여서 match(SEMI)를 여기에 넣었지만,
        // C-에서는 그럴 필요 X
        q = declaration();
        if (q != NULL) {
            if (t == NULL) t = p = q;
            else /* now p cannot be NULL either */
            {
                p->sibling = q;
                p = q;
            }
        }
    }
    return t;
}

// declaration은 var-declration이나 fun-declaration을 호출한다.
// var-declaration이나 fun-declaration은 둘 다 type-specifier ID까지 겹친다.
TreeNode* declaration(void)
{
    TreeNode* t = NULL;
    ExpType temp_type = type_specifier();
    char* val_or_fun_name = copyString(tokenString);

    match(ID);

    switch (token)
    {
    case SEMI:
        // var-declaration -> type-specifier ID; 인 경우
        t = newExpNode(checkVarK);
        if (t != NULL)
        {
            t->attr.name = val_or_fun_name;
            t->type = temp_type;
        }
        match(SEMI);
        break;

        // var-declaration -> type-specifier ID [ NUM ]; 인 경우
    case LBRACK: // '['
        t = newExpNode(checkArrayVarK);
        if (t != NULL)
        {
            t->attr.name = val_or_fun_name;
            t->type = temp_type;
        }
        match(LBRACK); // '['
        if (t != NULL)
            t->array_size = atoi(tokenString);
        match(NUM); // NUM 받고
        match(RBRACK); // ']'
        match(SEMI); // ';'
        break;

        // fun-declaration -> type-specifier ID ( params ) compund-stmt 인 경우
    case LPAREN: // '('

        t = newExpNode(fun_declarationK);
        if (t != NULL)
        {
            t->attr.name = val_or_fun_name;
            t->type = temp_type;
        }
        match(LPAREN); // '('
        if (t != NULL)
            t->child[0] = params();
        match(RPAREN); // ')'

        if (t != NULL)
            t->child[1] = compound_stmt();
        break;
    default: syntaxError("unexpected token(decl함수) -> ");
        printToken(token, tokenString);
        token = getToken();
        break;
    }
    return t;
}

TreeNode* var_declaration(void)
{
    TreeNode* t = NULL;

    ExpType temp_type = type_specifier();;
    char* val_or_fun_name = copyString(tokenString);

    match(ID);

    switch (token)
    {
    case SEMI:
        t = newExpNode(checkVarK);
        if (t != NULL)
        {
            t->attr.name = val_or_fun_name;
            t->type = temp_type;
        }
        match(SEMI);
        break;
    case LBRACK:
        t = newExpNode(checkArrayVarK);
        if (t != NULL)
        {
            t->attr.name = val_or_fun_name;
            t->type = temp_type;
        }
        match(LBRACK);
        if (t != NULL)
            t->array_size = atoi(tokenString);
        match(NUM);
        match(RBRACK);
        match(SEMI);
        break;
    default: syntaxError("unexpected token (var_decl함수) -> ");
        printToken(token, tokenString);
        if (token == RPAREN) {
            RPARENcheck = 1;
        }
        token = getToken();
        break;
    }
    return t;
}


TreeNode* fun_declaration(void)
{
    TreeNode* t = NULL;

    ExpType temp_type = type_specifier();;
    char* val_or_fun_name = copyString(tokenString);

    match(ID);

    switch (token)
    {
    case LPAREN: // '('
        t = newExpNode(fun_declarationK);
        if (t != NULL)
        {
            t->attr.name = val_or_fun_name;
            t->type = temp_type;
        }
        match(LPAREN); // '('
        if (t != NULL)
            t->child[0] = params();
        match(RPAREN); // ')'
        if (t != NULL)
            t->child[1] = compound_stmt();
        break;
    default: syntaxError("unexpected token(func함수) -> ");
        printToken(token, tokenString);
        token = getToken();
        break;
    }
    return t;
}

ExpType type_specifier(void)
{
    /*
    // reserved words
        ELSE, IF, INT, RETURN, VOID, WHILE,


    //ExpType is used for type checking
        typedef enum { Void, Integer } ExpType;

       에 의해 switch문 token은 INT와 VOID로 구분.
       리턴 시에는 Integer, Void 로 한다.
    */
    switch (token)
    {
    case INT:
        token = getToken();
        return Integer;
    case VOID:
        token = getToken();
        return Void;
    default: syntaxError("unexpected token(type함수) -> ");
        paramcheck = 1;
        printToken(token, tokenString);
        token = getToken();
        return Void;
    }
}
/*
A function declaration consists of a return type specifier, an identifier,
and a comma - separated list of parameters inside parentheses,
followed by acompound state­ ment with the code for the function.
If the return type of the function is void, then the function returns no
value(i.e., is a procedure).Parameters of a function are either
void(i.e., there are no parameters) or a list representing the function’s parameters.
Param­eters followed by brackets are array parameters whose size can vary.
Simple integer parameters are passed by value.
Array parameters are passed by reference(i.e., as pointers)
and must be matched by an array variable during a call.
Note that there are no parameters of type “fiinction.”
The parameters of a function have scope equal to the compound statement of the function declaration,
and each invocation of a fiinction has a separate set of parameters.
Functions may be recursive(to the extent that declaration before use allows).
*/

TreeNode* params(void)
{
    TreeNode* t = NULL;
    ExpType temp_type = type_specifier();

    // params -> void 인 경우
    if (temp_type == Void && token == RPAREN) {
        t = newExpNode(checkVarK);
        t->include_param = 1;
        t->type = Void;
        t->attr.name = "empty"; // 파라미터에 name 없이 type만 있을 경우 "empty"로 표시
    }
    // params -> param-list 인 경우
    else
        t = param_list(temp_type);
    return t;
}

// param_list는 EBNF로 했을 때
// param_list -> param { , param } 이 된다.
// 따라서, param_list는 param 먼저 호출
TreeNode* param_list(ExpType type)
{
    TreeNode* t = param(type);
    TreeNode* p = t;
    TreeNode* q;

    // while문을 통해 { , param } 반복 표현
    while (token == COMMA) {
        match(COMMA); // ','
        q = param(type_specifier());
        if (q != NULL) {
            if (t == NULL) t = p = q;
            else /* now p cannot be NULL either */
            {
                p->sibling = q;
                p = q;
            }
        }
    }
    return t;
}

// param함수는 EBNF로
// param -> type-soecifier ID [ [ ] ] 이므로
// type-specifier와 ID 매치를 해주고 [ ] 여부 판단

TreeNode* param(ExpType type)
{
    TreeNode* t = NULL;
    char* val_or_fun_name;

    val_or_fun_name = copyString(tokenString);
    match(ID);

    if (token == LBRACK)
    {
        match(LBRACK);
        match(RBRACK);
        t = newExpNode(checkArrayVarK);
    }
    else
        t = newExpNode(checkVarK);
    if (t != NULL)
    {
        t->attr.name = val_or_fun_name;
        t->type = type;
        t->include_param = 1;
    }
    return t;
}

// compoind_stmt -> { local-declarations statement-list }
TreeNode* compound_stmt(void)
{

    TreeNode* t = newStmtNode(compound_stmtK);

    match(LBRACE);

    t->child[0] = local_declarations();
    t->child[1] = statement_list();
    match(RBRACE);
    return t;
}

// local - declarations-> local-declarations var-declaration ㅣ empty 는 EBNF로
// local - declarations-> empty { var-declaration } 이므로 NULL처리 먼저.
TreeNode* local_declarations(void)
{
    TreeNode* t = NULL;
    TreeNode* p;

    if (token == INT || token == VOID) {
        t = var_declaration();
    }
    p = t;

    if (t != NULL)
    {
        while (token == INT || token == VOID) {
            TreeNode* q = var_declaration();
            if (q != NULL) {
                if (t == NULL) t = p = q;
                else /* now p cannot be NULL either */
                {
                    p->sibling = q;
                    p = q;
                }
            }
        }
    }
    return t;
}

// statement-list -> statement-list statement ㅣ empty
// EBNF로 statement-list -> empty { statement }
TreeNode* statement_list(void)
{
    if (token == RBRACE) { // ' } '
        return NULL;
    }

    TreeNode* t = statement();
    TreeNode* p = t;

    while (token != RBRACE) // ' } '
    {
        TreeNode* q = statement();
        if (q != NULL) {
            if (t == NULL) t = p = q;
            else /* now p cannot be NULL either */
            {
                p->sibling = q;
                p = q;
            }
        }
    }
    return t;
}

//////////////////////////////
TreeNode* parse1()
{
    TreeNode* t;
    t = declaration_list();
    if (token != ENDFILE)
        syntaxError("Code ends before file\n");
    return t;
}
/////////////////////////////////

// statement -> expression-stmt ㅣ compound-stmt l selection-stmt
//              ㅣ iteration-stmt ㅣ return-stmt
TreeNode* statement(void)
{
    TreeNode* t = NULL;
    switch (token)
    {
        /*
        An expression statement has an optional expression followed by a semicolon.
        Such expressions are usually evaluated for their side effects.
        Thus, this statement is used for assignments and function calls.
        */
    case ID:
    case LPAREN:
    case NUM:
    case SEMI:
        t = expression_stmt();
        break;

        // compound문 즉, 복합문은 선언 집합을 둘러싼 중괄호로 구성됨.
        // 복합 명령문은 주어진 순서대로 명령문 sequence를 실행
    case LBRACE: // ' { '
        t = compound_stmt();
        break;

        /*
        The if-statement has the usual semantics: the expression is evaluated;
        a nonzero value causes execution of the first statement;
        a zero value causes execution of the second statement
        This rule results in the classical dangling else ambiguity,
        which is resolved in the standard way: the else part is always parsed immediately as a
        substructure of the current if (the “most closely nested” disambiguating rule).
        */
    case IF: // reserved words = IF
        t = selection_stmt();
        break;

        /*
         The while-statement is the only iteration statement in C—.
         It is executed by repeatedly evaluating the expression and
         then executing the statement if the expression evaluates to a nonzero value,
         ending when the expression evaluates to 0.
        */
    case WHILE: // reserved words = WHILE
        t = iteration_stmt();
        break;

        /*
         A return statement may either return a value or not.
         Functions not declared void must return values.
         Functions declared void must not return values.
        */
    case RETURN: // reserved words = RETURN
        t = return_stmt();
        break;
    default: syntaxError("unexpected token(state함수) -> ");
        printToken(token, tokenString);
        if ((token == INT && paramcheck == 0) || token == VOID) { // int ((x<y) of int (x<y)) 와 같은 error 처리 부분
            fprintf(listing, "\n-- 해당 에러 구문 recovery :: 이어서 syntax tree 구성시작 --\n");
            Error = FALSE;
            TreeNode* syntaxTree = parse1();
            if (TraceParse) {
                fprintf(listing, "\nSyntax tree:\n");
                printTree(syntaxTree);
            }
            fclose(source);
            fclose(listing);

            exit(1);
        }
        token = getToken();
        return Void;
    }
    return t;
}

// expression_stmt() -> expression ; ㅣ ;
TreeNode* expression_stmt(void)
{
    TreeNode* t = NULL;

    if (token == SEMI) {
        match(SEMI);
    }
    else if (token != RBRACE)
    {
        t = expression();
        match(SEMI);
    }
    return t;
}

/*
selection-stmt -> if ( expression ) statement ㅣ if ( expression )                              statement else statement
EBNF 변경 => selection-stmt -> if ( expression ) statement [ else statement ]
*/
TreeNode* selection_stmt(void)
{
    TreeNode* t = newStmtNode(selection_stmtK);

    match(IF); // ' IF '
    match(LPAREN); // ' ( '

    if (t != NULL) {
        t->child[0] = expression();
    }
    match(RPAREN); // ')'
    if (t != NULL) {
        t->child[1] = statement();
    }
    // optional
    if (token == ELSE) {
        match(ELSE);
        if (t != NULL)
            t->child[2] = statement();
    }
    return t;
}

// iteration-stmt -> while (expression) statement
TreeNode* iteration_stmt(void)
{
    TreeNode* t = newStmtNode(iteration_stmtK);

    match(WHILE);

    match(LPAREN); // '('

    if (t != NULL)
        t->child[0] = expression();

    match(RPAREN); // ')'
    if (t != NULL)
        t->child[1] = statement();

    return t;
}

/*
 return-stmt -> return ; ㅣ return expression ; 를
EBNF로 return-stmt -> return [ expression ] ;
*/
TreeNode* return_stmt(void)
{
    TreeNode* t = newStmtNode(return_stmtK);

    match(RETURN);
    if (token != SEMI && t != NULL) {
        t->child[0] = expression();
    }
    match(SEMI);
    return t;
}

/*
expression -> var = expression ㅣ simple-expression
*/
TreeNode* expression(void)
{
    TreeNode* t = NULL;
    TreeNode* q = NULL;
    int check = 0;

    if (token == ID)
    {
        q = call();
        check = 1;
    }

    if (check == 1 && token == ASSIGN)
    {
        if (q != NULL && q->nodekind == ExpK && q->kind.exp == IdK)
        {
            match(ASSIGN);
            t = newExpNode(AssignK);
            if (t != NULL)
            {
                t->child[0] = q;
                t->child[1] = expression();
            }
        }
        else
        {
            syntaxError("unexpected\n");
            token = getToken();
        }
    }
    else
        t = simple_expression(q);
    return t;
}

/*
simple-expression -> additive-expression relop additive-expression ㅣ additive-expression
EBNF로 simple-expression -> additive-expression [ relop additive-expression ]
*/
TreeNode* simple_expression(TreeNode* g)
{
    TreeNode* t = NULL;
    TreeNode* q = additive_expression(g);
    // relop -> <= ㅣ < ㅣ > l >= l == l !=
    // relop -> LTE ㅣ LT ㅣ GT ㅣ GTE ㅣ EQ ㅣ NE
    if (token == LTE || token == LT || token == GT || token == GTE || token == EQ || token == NE)
    {
        TokenType temp_relop = token;
        match(token);
        t = newExpNode(OpK);

        if (t != NULL) {
            t->child[0] = q;
            t->child[1] = additive_expression(NULL);
            t->attr.op = temp_relop;
        }
    }
    else
        t = q;
    return t;
}

/*
additive-expression -> additive-expression addop term ㅣ term
EBNF로 additive-expression -> term { addop term }
*/
TreeNode* additive_expression(TreeNode* g)
{
    TreeNode* t = term(g);
    // addop -> +ㅣ-
    if (t != NULL) {
        while (token == PLUS || token == MINUS)
        {
            TreeNode* q = newExpNode(OpK);
            if (q != NULL) {
                q->child[0] = t;
                q->attr.op = token;
                t = q;

                match(token);

                t->child[1] = term(NULL);
            }
        }
    }
    return t;
}

/*
term -> term mulop factor ㅣ factor
EBNF로 term -> factor { mulop factor }
*/
TreeNode* term(TreeNode* g)
{
    TreeNode* t = factor(g);
    // mulop -> * ㅣ /
    if (t != NULL) {
        while (token == TIMES || token == OVER)
        {
            TreeNode* q = newExpNode(OpK);
            if (q != NULL) {
                q->child[0] = t;
                q->attr.op = token;
                t = q;

                match(token);

                t->child[1] = factor(NULL);
            }
        }
    }
    return t;
}

// factor -> ( expression ) ㅣ var ㅣ call ㅣ NUM
// var는 위에서 같이 처리
TreeNode* factor(TreeNode* g)
{
    if (g != NULL) {
        return g;
    }

    TreeNode* t = NULL;
    switch (token)
    {
    case LPAREN: // '('
        match(LPAREN);
        t = expression();
        match(RPAREN); // ')'
        break;
    case ID:
        t = call();
        break;
    case NUM:
        t = newExpNode(ConstK);
        if (t != NULL)
        {
            t->attr.val = atoi(tokenString);
            t->type = Integer;
        }
        match(NUM);
        break;
    default: syntaxError("unexpected token(fac함수) -> ");
        printToken(token, tokenString);
        token = getToken();
        return Void;
    }
    return t;
}


// call -> ID ( args )
TreeNode* call(void)
{
    TreeNode* t;
    char* val_or_fun_name = NULL;

    if (token == ID) {
        val_or_fun_name = copyString(tokenString);
    }
    match(ID);

    if (token == LPAREN)
    {
        match(LPAREN);
        t = newStmtNode(callK);
        if (t != NULL)
        {
            t->attr.name = val_or_fun_name;
            t->child[0] = args();
        }
        match(RPAREN);
    }
    else if (token == LBRACK)
    {
        t = newExpNode(IdK);
        if (t != NULL)
        {
            t->attr.name = val_or_fun_name;
            t->type = Integer;
            match(LBRACK);
            t->child[0] = expression();
            match(RBRACK);
        }
    }
    else
    {
        t = newExpNode(IdK);
        if (t != NULL)
        {
            t->attr.name = val_or_fun_name;
            t->type = Integer;
        }
    }
    return t;
}

TreeNode* args(void)
{
    if (token == RPAREN) {
        return NULL;
    }
    else {
        return arg_list();
    }
}

/*
arg-list -> arg-list , expression ㅣ expression
EBNF로 arg-list -> arg-list { , expression }
*/

TreeNode* arg_list(void)
{
    TreeNode* t = expression();
    TreeNode* p = t;

    if (t != NULL) {
        while (token == COMMA)
        {
            match(COMMA);
            TreeNode* q = expression();
            if (q != NULL) {
                if (t == NULL) t = p = q;
                else /* now p cannot be NULL either */
                {
                    p->sibling = q;
                    p = q;
                }
            }
        }
    }
    return t;
}

/****************************************/
/* the primary function of the parser   */
/****************************************/
/* Function parse returns the newly
 * constructed syntax tree
 */
TreeNode* parse()
{
    TreeNode* t;
    token = getToken();
    t = declaration_list();
    if (token != ENDFILE)
        syntaxError("Code ends before file\n");
    return t;
}


///////////////////////////////////////////////////   MAIN.C 파일    //////////////////////////
int main(int argc, char* argv[]) {
    TreeNode* syntaxTree;
    char PFile[120]; /* 스캔한 결과 출력대상 파일*/
    char SFile[120]; /* source code file name */
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <filename>\n", argv[0]);
        exit(1);
    }

    //source code file 처리작업
    strcpy(SFile, argv[1]);
    if (strchr(SFile, '.') == NULL)
        strcat(SFile, ".c");
    source = fopen(SFile, "r");

    // 스캔한 결과 출력대상 file 처리작업
    strcpy(PFile, argv[2]);
    if (strchr(PFile, '.') == NULL)
        strcat(PFile, ".txt");
    listing = fopen(PFile, "w");

    if (source == NULL)
    {
        fprintf(stderr, "File %s not found\n", SFile);
        exit(1);
    }
    //fprintf(listing, "\nTINY COMPILATION: %s\n", SFile); // 그냥 TINY COMPILATION으로 칭함.
    //fprintf(listing, "\nC- language: %s\n", SFile);

    //while (getToken() != ENDFILE);

    syntaxTree = parse();
    if (TraceParse) {
        fprintf(listing, "\nSyntax tree:\n");
        printTree(syntaxTree);
    }

    // 파일닫기
    fclose(source);
    fclose(listing);

    return 0;
}