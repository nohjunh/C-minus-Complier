#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
Scanner구현은 Scanner만을 위해 각 헤더파일, C파일을 나누지 않고 하나의 Scan.c에서 처리함.
단, 후에 구현의 편리함을 주석으로 경계구분.
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

/* EchoSource = TRUE causes the source program to
 * be echoed to the listing file with line numbers
 * during parsing
 */
int EchoSource = TRUE;


/* TraceScan = TRUE causes token information to be
 * printed to the listing file as each token is
 * recognized by the scanner
 */
int TraceScan = TRUE;


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
    }/
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

///////////////////////////////////////////////////   MAIN.C 파일    //////////////////////////
int main(int argc, char* argv[]) {
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
    fprintf(listing, "\nC- language: %s\n", SFile);


    while (getToken() != ENDFILE);

    return 0;
}