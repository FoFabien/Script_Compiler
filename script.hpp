#ifndef SCRIPT_HPP
#define SCRIPT_HPP

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <set>
#include <list>
#include <stack>
#include <utility>
#include <functional>

// enum used at compile and run time
enum {INVALID, STR, INT, FLOAT, OPERATOR, LBRK, RBRK, COMMA, LCUR, RCUR, FUNC, VAR, RESULT, CVAR, COP, CFUNC, GFUNC, GVAR, TBD};
//***************************************************************************************************************
// COMPILE
//***************************************************************************************************************
enum {UNDEF, PREFIX, INFIX, POSTFIX};

struct Token
{
    std::string s; // token content
    int t = INVALID; // token type
    int o = UNDEF; // token tag

    Token() {}
    Token(const std::string& s, const int& t, const int& o = UNDEF): s(s), t(t), o(o) {}
    Token(Token &cpy): s(cpy.s), t(cpy.t), o(cpy.o) {}

    bool isIntValue() const { return (t == INT || t == RESULT || t == CVAR || t == COP || t == GVAR); }
    bool isFloatValue() const { return (t == FLOAT); }
    bool isStringValue() const { return (t == STR); }
    bool isNumber() const { return (t == INT || t == FLOAT); }
    std::string getStrippedString() const { return (s.size() < 2 ? "" : s.substr(1, s.size()-2)); }
    void inverseSign()
    {
        switch(t)
        {
            case INT: s = std::to_string(-std::stoi(s)); break;
            case FLOAT:s = std::to_string(-std::stof(s)); break;
            default: break;
        }
    }
    int getInt() const { return std::stoi(s); }
    float getFloat() const { return std::stof(s); }
    bool operator==(const Token& rhs) { return (t == rhs.t) && (s == rhs.s) && (o == rhs.o); }
};

typedef std::vector<std::vector<Token*> > TokenList;
typedef std::unordered_map<std::string, TokenList> Program;
typedef std::unordered_map<std::string, std::set<std::string> > VariableList;
typedef std::vector<std::string> TokenIDList;

struct Instruction
{
    void clear()
    {
        delete op;
        for(auto i: params)
            delete i;
    };
    Token* op = nullptr;
    std::vector<Token*> params;
    bool hasResult = false;
};

struct Code
{
    std::vector<std::string> var;
    std::vector<Instruction> line;
    size_t creg = 0;
    size_t argn = 0;
};

typedef std::unordered_map<std::string, Code> Compiled;

//***************************************************************************************************************
// RUN
//***************************************************************************************************************
class Value
{
    public:
        Value(): p(nullptr), t(TBD) {};
        void clear(); // reminder: the memory must be FREE using clear
        bool set(const void *any, const int& type);
        bool set(const int& v);
        bool set(const float& v);
        bool set(const std::string& v);
        const int& getType() const { return t; }
        const void* getP() const { return p; }
        template <class T> const T* get() const { return (T*)p;}
        bool operator==(const Value& rhs);

    private:
        void* p;
        int t;
};

struct Line
{
    Value op;
    std::vector<Value> params;
    bool hasResult;
};
struct Function
{
    size_t argn;
    size_t varn;
    size_t regn;
    std::vector<Line> line;
    std::map<int, int> while_map;
};
typedef std::vector<Function> Runtime;

class Script;
typedef void (*Callback)(Script*, Line&);
typedef std::unordered_map<std::string, Callback>::iterator CallRef;

struct IfPos
{
    size_t scope;
    int pc;
};

struct RunState
{
    int pc;
    size_t id;
    size_t scope;
    std::stack<IfPos> ifstack;
    std::vector<Value> vars;
    std::vector<Value> regs;
};

//***************************************************************************************************************
// MAIN CLASS
//***************************************************************************************************************
class Script
{
    public:
        enum { NONE = 0, PRINT = 1 }; // flags

        Script();
        virtual ~Script();
        bool load(const std::string& file);
        bool run();
        void setError(const std::string& err = "");
        void setVar(const int& i, const int& v, const int &type); // set the variable content to v (i is the variable id, type is CVAR, GVAR, RESULT)
        void setVar(const int& i, const std::string& v, const int &type);
        void setVar(const int& i, const float& v, const int &type);
        void setVar(const int& i, const Value& v, const int &type);
        Value& getVar(const Value& v); // get the variable content (setError() if it's not a variable)
        const void* getValueContent(const Value& v, int &type); // get content and type stored in v. If v is a variable, return the variable content

        static bool compile(const std::string& file, const std::string& output, const char &flag = NONE);

        static void addGlobalFunction(const std::string& name, Callback callback, const size_t &argn);
        static void initGlobalVariables(const size_t& n);
        static std::vector<Value>& getGlobalVariables();
        static void clearGlobalVariables();

        static void _if(Script* s, Line& l);
        static void _else(Script* s, Line& l);
        static void _return(Script* s, Line& l);
        static void _while(Script* s, Line& l);
        static void _print(Script* s, Line& l);
        static void _debug(Script* s, Line& l);
        static void _break(Script* s, Line& l);

        bool rejectReturn(const Line& l);
        void funcReturn(const Value& v, Line& l);
        void funcReturn(const int& v, Line& l);
        void funcReturn(const float& v, Line& l);
        void funcReturn(const std::string& v, Line& l);

    protected:
        static bool shuntingyard(const TokenIDList& tokens, Compiled& code);
        static bool format(Program &prog, VariableList &vars, Compiled& code);
        static int errorCheck(Compiled& code);
        static bool postprocessing(Compiled& code);
        static bool save(const std::string& output, const Compiled& code);
        static void print(Compiled& code);
        static void debug(Program& code);

        void operation(Line& line);
        void enterBlock(const bool& loop);
        void skipBlock(const bool& checkElse);
        int get_while_loop_point();
        void push_stack(Line& line);
        void ret(const Value* v);

        // debug
        void printValue(const Value& v, const bool& isContent = false);

        bool loaded;
        enum {STOP, ERROR, PAUSE, PLAY} state;
        Runtime code;
        size_t entrypoint;
        int pc;
        size_t scope;
        size_t id;
        bool canElse;
        std::vector<Value> currentVars;
        std::vector<Value> currentRegs;
        std::stack<IfPos> ifstack;
        std::stack<RunState> call_stack;
        std::stack<Value*> return_stack;
};

#endif // SCRIPT_HPP
