// Lexer for search queries

public enum Token {
  TOKEN_END = 0,
  TOKEN_UNKNOWN,   // unknown character

  TOKEN_PLUS,
  TOKEN_MINUS,
  TOKEN_COLON,

  TOKEN_EQUAL,
  TOKEN_LESSEQUAL,
  TOKEN_GREATEREQUAL,
  TOKEN_NOTEQUAL,  // <> or !=
  TOKEN_LESS,
  TOKEN_GREATER,

  TOKEN_LPAREN,
  TOKEN_RPAREN,
  TOKEN_LBRACKET,
  TOKEN_RBRACKET,

  TOKEN_PHRASE,

  TOKEN_WORD,      // hello
  TOKEN_ASIANWORD,

  TOKEN_AND,
  TOKEN_OR,
}

static mapping(string : Token) keywords = ([
  //  "not" : TOKEN_NOT,
  "and" : TOKEN_AND,
  "or" : TOKEN_OR,
]);

static multiset(int) specialChars = (<
  ':', '(', ')', ','
>);

int isDigit(int ch) { return spider.isdigit(ch); }
int isWordChar(int ch) { return spider.isnamechar(ch) && !specialChars[ch]; }
int isWhiteSpace(int ch) { return ch == '\t' || ch == ' '; }
int isDateChar(int ch) { return spider.isnamechar(ch) || isWhiteSpace(ch);  }

int isAsianWord(int ch) { return 0; }

//!   Tokenizes a query into tokens for later use by a parser.
//! @param query
//!   The query to tokenize.
//! @returns
//!   An array containing the tokens:
//!     @tt{ ({ ({ TOKEN_WORD, "foo" }), ... }) @}
//!   Or, in case of an error, a string with the error message.
public string|array(array(Token|string)) tokenize(string query) {
  array(array(Token|string)) result = ({});
  int len = strlen(query);
  query += "\0";

  int pos = 0;

  for (;;) {
    string x = query[pos .. pos];
#define EMIT(tok) EMIT2(tok,x)
#define EMIT2(tok,str) result += ({ ({ tok, str, str }) })
    switch (x) {
      case "\0":
        EMIT(TOKEN_END);
        return result;
      case "\t":
      case " ":
        // whitespace ignored.
        if (sizeof(result))
          result[-1][2] += x;
        break;
      case "\"":
      case "\'":
        string s;
        int end = search(query, x, pos + 1);
        if (end < 0) {
          s = query[pos + 1 .. len - 1];
          pos = len - 1;
        }
        else {
          s = query[pos + 1 .. end - 1];
          pos = end;
        }
        EMIT2(TOKEN_PHRASE, s);
        break;
      case "+": EMIT(TOKEN_PLUS);       break;
      case "-": EMIT(TOKEN_MINUS);      break;
      case "=": EMIT(TOKEN_EQUAL);      break;
      case "(": EMIT(TOKEN_LPAREN);     break;
      case ")": EMIT(TOKEN_RPAREN);     break;
      case "[": EMIT(TOKEN_LBRACKET);   break;
      case "]": EMIT(TOKEN_RBRACKET);   break;
      case ":": EMIT(TOKEN_COLON);      break;
      case "<":
        if (query[pos + 1] == '=') {
          ++pos;
          EMIT2(TOKEN_LESSEQUAL, "=>");
        }
        else if (query[pos + 1] == '>') {
          ++pos;
          EMIT2(TOKEN_NOTEQUAL, "<>");
        }
        else
          EMIT(TOKEN_LESS);
        break;
      case ">":
        if (query[pos + 1] == '=') {
          ++pos;
          EMIT2(TOKEN_GREATEREQUAL, ">=");
        }
        else
          EMIT(TOKEN_GREATER);
        break;
      case "!":
        if (query[pos + 1] == '=') {
          ++pos;
          EMIT2(TOKEN_NOTEQUAL, "!=");
        }
        else
          EMIT(TOKEN_UNKNOWN);
        break;
      default:
        if (isAsianWord(query[pos]))
          EMIT(TOKEN_ASIANWORD);
        else if (isWordChar(query[pos])) {
          int i = pos + 1;
          while (isWordChar(query[i]))
            ++i;
          string word = query[pos .. i - 1];
          string lword = lower_case(word);
          //
          if (keywords[lword])
            EMIT2(keywords[lword], word);
          else
            EMIT2(TOKEN_WORD, word);
          pos = i - 1;
        }
        else
          EMIT(TOKEN_UNKNOWN);
    }
    ++pos;
  }

#undef EMIT
#undef EMIT2

}
