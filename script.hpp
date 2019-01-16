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

//***************************************************************************************************************
// COMPILE
//***************************************************************************************************************
enum {INVALID, STR, INT, FLOAT, OPERATOR, LBRK, RBRK, COMMA, LCUR, RCUR, FUNC, VAR, RESULT, CVAR, COP, CFUNC, TBD};
enum {UNDEF, PREFIX, INFIX, POSTFIX};

class Token
{
    public:
        Token();
        ~Token();
        static Token* make(const std::string& s, const int& t, const int& o = UNDEF);
        static Token* make(Token &cpy);
        bool isIntValue() const;
        bool isFloatValue() const;
        bool isStringValue() const;
        bool isDigit() const;
        std::string getString() const;
        std::string getStrippedString() const;
        void inverseSign();
        int getInt() const;
        float getFloat() const;
        int getType() const;
        int getTag() const;
        bool operator==(const Token& rhs);

    private:
        std::string s;
        int i;
        float f;
        int t;
        int o;
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
        Value();
        ~Value();
        void clear();
        bool set(const void *any, const int& type);
        bool set(const int& v);
        bool set(const float& v);
        bool set(const std::string& v);
        const int& getType() const;
        const void* getP() const;
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
typedef const std::function<void(Script*, Line&)> Callback;

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
        void setError();
        void setVar(const int& i, const int& v, const int &type);
        void setVar(const int& i, const std::string& v, const int &type);
        void setVar(const int& i, const float& v, const int &type);
        void setVar(const int& i, const Value& v, const int &type);
        Value& getVar(const int& i, const int &type);
        const void* getValueContent(const Value& v, int &type);

        void enterScope(const bool& loop);
        void skipScope(const bool& checkElse);
        void push_stack(Value* v);
        void ret(const Value* v);
        void ret(const int& v);
        void ret(const float& v);
        void ret(const std::string& v);

        static bool compile(const std::string& file, const std::string& output, const char &flag = NONE);

        static void addGlobalFunction(const std::string& name, Callback& callback, const size_t &argn);

        static void _if(Script* s, Line& l);
        static void _else(Script* s, Line& l);
        static void _return(Script* s, Line& l);
        static void _while(Script* s, Line& l);
        static void _print(Script* s, Line& l);
        static void _debug(Script* s, Line& l);
        static void _break(Script* s, Line& l);

    protected:
        static bool shuntingyard(const TokenIDList& tokens, Compiled& code);
        static bool format(Program &prog, VariableList &vars, Compiled& code);
        static int errorCheck(Compiled& code);
        static bool optimize(Compiled& code);
        static bool save(const std::string& output, const Compiled& code);
        static void print(Compiled& code);
        static void debug(Program& code);

        void operation(Line& line);
        int get_while_loop_point();

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
