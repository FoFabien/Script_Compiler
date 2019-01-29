#include "script.hpp"
#include <fstream>
#include <sstream>
#include <cctype>

#include <iostream>

static std::unordered_map<std::string, size_t> gl_func = {{"if", 1}, {"else", 0}, {"elif", 1}, {"return", 1}, {"while", 1}, {"print", 1}, {"debug", 1}, {"break", 0}};
static std::unordered_map<std::string, Callback> gl_callback = {{"if", Script::_if}, {"else", Script::_else}, {"elif", Script::_if}, {"return", Script::_return}, {"while", Script::_while}, {"print", Script::_print}, {"debug", Script::_debug}, {"break", Script::_break}};
static std::vector<Value> globalVars;
#define SCRIPT_MAGIC 0x89191500

//***************************************************************************************************************
// COMPILE
//***************************************************************************************************************

static const std::unordered_map<std::string, size_t> op_unordered_map = {
{"=", 0}, {"+", 1}, {"-", 2}, {"*", 3}, {"/", 4}, {"!", 5}, {"!=", 6}, {">", 7}, {"<", 8}, {">=", 9}, {"<=", 10},
{"==", 11}, {"&", 12}, {"^", 13}, {"|", 14}, {"&&", 15}, {"||", 16}, {"^^", 17}, {"++", 18}, {"--", 19}, {"+=", 20}, {"-=", 21},
{"*=", 22}, {"/=", 23},  {"%", 24},  {"%=", 25}
};
static const std::unordered_map<std::string, int> op_list = {
{"=", 0}, {"+", 3}, {"-", 3}, {"*", 4}, {"/", 4}, {"%", 4}, {"%=", 0}, {"!", 2}, {"!=", 1}, {">", 1}, {"<", 1}, {">=", 1}, {"<=", 1},
{"==", 1}, {"&", 5}, {"^", 5}, {"|", 5}, {"&&", 2}, {"||", 2}, {"^^", 2}, {"++", 6}, {"--", 6}, {"+=", 0}, {"-=", 0}, {"*=", 0}, {"/=", 0}
};

static bool isFunction(const std::string &f, const Compiled &c)
{
    return (gl_func.find(f) != gl_func.end() || c.find(f) != c.end());
}

static bool isCondition(const std::string &f)
{
    return (f == "if" || f == "else" || f == "elif" || f == "while");
}

static bool isName(const std::string &name)
{
    if(isdigit(name[0])) return false;
    for(auto &c: name)
    {
        if(!isalnum(c) && c != '_')
            return false;
    }
    return true;
}

static int detectType(const std::string &s)
{
    if(s.size() >= 2 && s[0] == '"' && s.back() == '"')
        return 0; // string

    if(s.empty()) return -1;

    if(isdigit(s[0]) || s[0] == '-')
    {
        char dot = 0;
        for(size_t i = 0; i < s.size(); ++i)
        {
            switch(s[i])
            {
                case '-':
                    if(i == 0) dot = -1;
                    else return -1;
                    break;
                case '.':
                    if(dot > 0 || i - dot == 0 || i == s.size()-1) return -1;
                    dot = 1;
                    break;
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                    continue;
                default:
                    return -1;
            }
        }
        return (dot == 1 ? 2 : 1); // float : int
    }
    else if(s[0] == '@')
    {
        if(s.size() == 1) return -1;
        for(size_t i = 1; i < s.size(); ++i)
        {
            if(!std::isdigit(s[i])) return -1;
        }
        return 4; // gvar
    }
    else
    {
        for(auto &c: s)
        {
            if(!isalnum(c) && c != '_')
                return -1;
        }
        return 3; // name
    }
}

static bool isSingleOp(const std::string &op)
{
    // checking: (op == "!" || op == "++" || op == "--");

    int i = op.size();
    if(i < 1 || i > 2)
        return false;
    switch(op[0])
    {
        case '!':
            if(i == 1) return true;
            return false;
        case '+': case '-':
            if(i == 2 && op[1] == op[0]) return true;
            return false;
        default:
            return false;
    }
}

static bool isPrefixOp(const std::string &op)
{
    //checking: (op == "-" || op == "!" || op == "++" || op == "--");

    return (isSingleOp(op) || op == "-");
}

static bool isInfixOp(const std::string &op)
{
    /*checking: (op == "=" || op == "+" || op == "-" || op == "*" || op == "/" || op == "!=" || op == ">" || op == "<" || op == ">=" || op == "<=" || op == "!=" || op == "==" || op == "&" || op == "^"
            || op == "|" || op == "&&" || op == "||" || op == "^^" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%" || op == "%=");
    */

    int i = op.size();
    if(i < 1 || i > 2)
        return false;
    switch(op[0])
    {
        case '!':
            return (i == 2 && op[1] == '=');
        case '&': case '|': case '^':
            return ((i == 2 && op[1] == op[0]) || i == 1);
        case '=': case '+': case '-': case '*': case '/': case '%': case '<': case '>':
            return ((i == 2 && op[1] == '=') || i == 1);
        default:
            return false;
    }
}

static bool isPostfixOp(const std::string &op)
{
    return (op == "++" || op == "--");
}

static bool greater_precedence(const std::string &op1, const std::string &op2)
{
    return op_list.at(op1) > op_list.at(op2);
}

static bool equal_precedence(const std::string &op1, const std::string &op2)
{
    return op_list.at(op1) == op_list.at(op2);
}

//***************************************************************************************************************
// RUN
//***************************************************************************************************************
Value::Value()
{
    t = TBD;
    p = nullptr;
}

Value::~Value()
{

}

void Value::clear()
{
    switch(t)
    {
        case FUNC: case STR: delete (std::string*)p; break;
        case INT: case COP: case RESULT: case CVAR: case CFUNC: case GVAR: delete (int*)p; break;
        case FLOAT: delete (float*)p; break;
        case GFUNC: delete (CallRef*)p; break;
        case LCUR: case RCUR: default: break;
    }
    t = TBD;
}

bool Value::set(const void *any, const int& type)
{
    switch(type)
    {
        case STR: case FUNC:
            if(t != type) { clear(); p = new std::string(*(const std::string*)any); }
            else (*(std::string*)p) = (*(const std::string*)any);
            break;
        case INT: case COP: case RESULT: case CVAR: case CFUNC: case GVAR:
            if(t != type) { clear(); p = new int(*(const int*)any); }
            else (*(int*)p) = (*(const int*)any);
            break;
        case FLOAT:
            if(t != type) { clear(); p = new float(*(const float*)any); }
            else (*(float*)p) = (*(const float*)any);
            break;
        case GFUNC:
            if(t != GFUNC) { clear(); p = new CallRef(*(const CallRef*)any); }
            else (*(CallRef*)p) = (*(const CallRef*)any);
            break;
        case LCUR: case RCUR:
            if(t != type) { clear(); p = nullptr; }
            break;
        default: return false;
    }
    t = type;
    return true;
}

bool Value::set(const int& v)
{
    if(t == INT) *((int*)p) = v;
    else { clear(); p = new int(v); t = INT; }
    return true;
}

bool Value::set(const float& v)
{
    if(t == FLOAT) *((float*)p) = v;
    else { clear(); p = new float(v); t = FLOAT; }
    return true;
}

bool Value::set(const std::string& v)
{
    if(t == STR) *((std::string*)p) = v;
    else { clear(); p = new std::string(v); t = STR; }
    return true;
}

bool Value::operator==(const Value& rhs)
{
    if(t != rhs.t) return false;
    switch(t)
    {
        case INT: case RESULT: case CVAR: case COP: case GVAR: case CFUNC: case GFUNC:
            return (*(int*)p == *(int*)rhs.p);
        case FLOAT:
            return (*(float*)p == *(float*)rhs.p);
        case STR: case OPERATOR: case LBRK: case RBRK: case COMMA: case LCUR: case RCUR: case FUNC: case VAR: case TBD:
            return (*(std::string*)p == *(std::string*)rhs.p);
        default:
            return false;
    }
}

//***************************************************************************************************************
// MAIN CLASS
//***************************************************************************************************************
Script::Script()
{
    loaded = false;
    state = STOP;
    entrypoint = SIZE_MAX;
}

Script::~Script()
{
    for(auto &i: code)
    {
        for(auto &j: i.line)
        {
            j.op.clear();
            for(auto &k: j.params) k.clear();
        }
    }
    for(auto &i: currentVars) i.clear();
    for(auto &i: currentRegs) i.clear();
    while(!call_stack.empty())
    {
        RunState& r = call_stack.top();
        for(auto &i: r.vars) i.clear();
        call_stack.pop();
    }
}

bool Script::load(const std::string& file)
{
    if(loaded) return false;
    loaded = true;

    // tmp vars used during the writing
    size_t tmp;
    char c;
    float fv;
    std::string buf;
    std::vector<std::string> lfunc;

    // saving the result
    // (file format subject to change)
    // output file
    std::ifstream f(file, std::ios::in | std::ios::binary);
    if(!f)
        return false;

    f.read((char*)&tmp, 4);
    if(tmp != SCRIPT_MAGIC) return false;
    f.read((char*)&tmp, 4);
    code.resize(tmp);
    lfunc.resize(tmp, "");

    for(size_t i = 0; i < lfunc.size(); ++i)
    {
        f.read((char*)&tmp, 4);
        if(tmp)
        {
            buf.resize(tmp);
            f.read(&(buf[0]), tmp);
            lfunc[i] = buf;
        }
        else entrypoint = i;
        f.read((char*)&tmp, 4);
        code[i].argn = tmp;
    }
    if(entrypoint >= lfunc.size()) return false;

    for(size_t i = 0; i < lfunc.size(); ++i)
    {
        Function& func = code[i];

        if(!f.good()) return false;

        f.read((char*)&func.regn, 4);
        f.read((char*)&func.varn, 4);
        f.read((char*)&tmp, 4);
        func.line.resize(tmp);

        id = i;
        pc = 0;

        for(auto &xi: func.line)
        {
            if(!f.good()) return false;
            f.read(&c, 1);
            switch(c)
            {
                case FUNC:
                {
                    f.read((char*)&tmp, 4);
                    buf = "";
                    if(tmp)
                    {
                        buf.resize(tmp);
                        f.read(&(buf[0]), tmp);
                    }
                    auto itf = gl_callback.find(buf);
                    if(itf != gl_callback.end())
                    {
                        xi.op.set(&itf, GFUNC);
                    }
                    else
                    {
                        size_t fid = 0;
                        for(; fid < lfunc.size(); ++fid)
                        {
                            if(buf == lfunc[fid])
                                break;
                        }
                        if(fid == lfunc.size())
                            return false;
                        xi.op.set(&fid, CFUNC);
                    }
                    break;
                }
                case COP: case CFUNC:
                    f.read((char*)&tmp, 4);
                    xi.op.set(&tmp, c);
                    break;
                case LCUR: case RCUR:
                    f.read((char*)&tmp, 4);
                    if(tmp != 1) return false;
                    f.read((char*)&tmp, 1);
                    xi.op.set(nullptr, c);
                    break;
                default: return false;
            }

            f.read((char*)&(xi.hasResult), 1);
            f.read((char*)&tmp, 4);
            xi.params.resize(tmp);

            for(auto &xj: xi.params)
            {
                if(!f.good()) return false;
                f.read(&c, 1);
                switch(c)
                {
                    case STR:
                        f.read((char*)&tmp, 4);
                        buf = "";
                        if(tmp)
                        {
                            buf.resize(tmp);
                            f.read(&(buf[0]), tmp);
                        }
                        xj.set(&buf, STR);
                        break;
                    case INT: case RESULT: case CVAR: case GVAR:
                        f.read((char*)&tmp, 4);
                        xj.set(&tmp, c);
                        break;
                    case FLOAT:
                        f.read((char*)&fv, 4);
                        xj.set(fv);
                        break;
                    default: break;
                }
            }
            if(xi.op.getType() == FUNC && *xi.op.get<std::string>() == "while")
                get_while_loop_point();
            ++pc;
        }
    }

    currentRegs.resize(code[entrypoint].regn);
    currentVars.resize(code[entrypoint].varn);
    state = STOP;
    return true;
}

bool Script::run()
{
    if(!loaded) return false;
    switch(state)
    {
        case ERROR: case PLAY: return false;
        case STOP:
            scope = 0;
            id = entrypoint;
            pc = 0;
            canElse = false;
        case PAUSE:
            state = PLAY;
            break;
        default:
            return false;
    }

    for(; pc < (int)code[id].line.size(); ++pc)
    {
        Line& line = code[id].line[pc];
        //std::cout << id << " -> " << pc << " : " << scope << " | " << ifstack.size() << std::endl;
        switch(line.op.getType())
        {
            case GFUNC:
            {
                const CallRef& foo = *(line.op.get<CallRef>());
                foo->second(this, line);
                break;
            }
            case COP:
                operation(line);
                break;
            case CFUNC:
            {
                std::vector<const Value*> args;
                size_t n = line.params.size() - (line.hasResult ? 1 : 0);
                for(size_t i = 0; i < n; ++i)
                {
                    switch(line.params[i].getType())
                    {
                        case INT: case FLOAT: case STR: args.push_back(&line.params[i]); break;
                        case RESULT: case CVAR: args.push_back(&getVar(*(line.params[i].get<int>()), line.params[i].getType())); break;
                        default: setError("invalid parameter #" + std::to_string(i)); return false;
                    }
                }
                if(line.hasResult)
                {
                    switch(line.params.back().getType())
                    {
                        case RESULT: case CVAR:
                        {
                            push_stack(&line.params.back());
                            break;
                        }
                        default: setError("invalid result value"); return false;
                    }
                }
                else push_stack(nullptr);
                id = *(line.op.get<int>());
                currentRegs.resize(code[id].regn);
                currentVars.resize(code[id].varn);
                for(size_t i = 0; i < code[id].argn; ++i)
                {
                    setVar(i, *args[i], CVAR);
                }
                break;
            }
            case LCUR:
                setError("unexpected block start");
                return false;
            case RCUR:
                scope--;
                if(!ifstack.empty())
                {
                    IfPos& ip = ifstack.top();
                    if(ip.scope == scope)
                    {
                        pc = ip.pc;
                        ifstack.pop();
                    }
                }
                if(scope < 0)
                {
                    setError("unexpected block end");
                }
                break;
            default:
                setError("invalid instruction (type: " + std::to_string(line.op.getType()));
                return false;
        }
        if(pc == (int)code[id].line.size() - 1)
        {
            ret(nullptr);
        }
        switch(state)
        {
            case PLAY: break;
            case PAUSE: ++pc; return true;
            default: return false;
        }
    }
    state = STOP;
    return false;
}

void Script::setError(const std::string& err)
{
    state = ERROR;
    std::cout << "Error flag raised" << std::endl;
    std::cout << "pc=" << pc << ", scope=" << scope << ", id=" << id << std::endl;
    if(!err.empty())
        std::cout << "Message: " << err << std::endl;
}

void Script::setVar(const int& i, const int& v, const int &type)
{
    Value* p;
    switch(type)
    {
        case RESULT: p = &(currentRegs[i]); break;
        case CVAR: p = &(currentVars[i]); break;
        case GVAR: p = &(globalVars[i]); break;
        default: setError("setVar(int) error"); return;
    }
    if(!p->set(v))
        setError("set(int) error");
}

void Script::setVar(const int& i, const std::string& v, const int &type)
{
    Value* p;
    switch(type)
    {
        case RESULT: p = &(currentRegs[i]); break;
        case CVAR: p = &(currentVars[i]); break;
        case GVAR: p = &(globalVars[i]); break;
        default: setError("setVar(string) error"); return;
    }
    if(!p->set(v))
        setError("set(string) error");
}

void Script::setVar(const int& i, const float& v, const int &type)
{
    Value* p;
    switch(type)
    {
        case RESULT: p = &(currentRegs[i]); break;
        case CVAR: p = &(currentVars[i]); break;
        case GVAR: p = &(globalVars[i]); break;
        default: setError("setVar(float) error"); return;
    }
    if(!p->set(v))
        setError("set(float) error");
}

void Script::setVar(const int& i, const Value& v, const int &type)
{
    Value* p;
    switch(type)
    {
        case RESULT: p = &(currentRegs[i]); break;
        case CVAR: p = &(currentVars[i]); break;
        case GVAR: p = &(globalVars[i]); break;
        default: setError("setVar(Value) error"); return;
    }
    if(!p->set(v.getP(), v.getType()))
        setError("set(Value) error");
}

Value& Script::getVar(const int& i, const int &type)
{
    switch(type)
    {
        case RESULT: return currentRegs[i];
        case CVAR: return currentVars[i];
        case GVAR: return globalVars[i];
        default: setError("getVar() error"); return code[id].line[pc].op;
    }
}

const void* Script::getValueContent(const Value& v, int &type)
{
    const void* p = nullptr;
    switch(v.getType())
    {
        case INT: case FLOAT: case STR: p = v.getP(); type = v.getType(); break;
        case CVAR: case RESULT: case GVAR:
        {
            Value& w = getVar(*v.get<int>(), v.getType());
            switch(w.getType())
            {
                case INT: case FLOAT: case STR: p = w.getP(); type = w.getType(); break;
                default: return nullptr;
            }
            break;
        }
        default: return nullptr;
    }
    return p;
}

void Script::enterBlock(const bool& loop)
{
    if(pc+1 >= (int)code[id].line.size() || code[id].line[pc+1].op.getType() != LCUR)
    {
        setError("can't enter block, unexpected end of function");
        return;
    }
    canElse = false;
    if(loop)
    {
        ifstack.push(IfPos());
        IfPos& ip = ifstack.top();
        ip.pc = get_while_loop_point()-1;
        ip.scope = scope;
    }
    pc++;
    scope++;
}

void Script::skipBlock(const bool& checkElse)
{
    if(pc+1 >= (int)code[id].line.size() || code[id].line[pc+1].op.getType() != LCUR)
    {
        setError("can't skip block, unexpected end of function");
        return;
    }
    pc += 2;
    int current = scope+1;
    for(; pc < (int)code[id].line.size() && current > (int)scope; ++pc)
    {
        switch(code[id].line[pc].op.getType())
        {
            case LCUR: ++current; break;
            case RCUR: --current; break;
            default: break;
        }
    }
    --pc;
    if((int)current != current)
    {
        setError("malformed block error");
        return;
    }
    canElse = checkElse;
}

void Script::push_stack(Value* v)
{
    if(v) return_stack.push(&getVar(*v->get<int>(), v->getType()));
    else return_stack.push(nullptr);
    call_stack.push(RunState());
    RunState &tmp = call_stack.top();
    tmp.pc = pc;
    tmp.id = id;
    tmp.scope = scope;
    tmp.ifstack.swap(ifstack);
    tmp.vars.swap(currentVars);
    tmp.regs.swap(currentRegs);
    pc = -1;
}

void Script::ret(const Value* v)
{
    if(return_stack.empty())
    {
        if(id != entrypoint) setError("return stack is empty");
        else state = STOP;
    }
    else
    {
        Value* p = return_stack.top();
        return_stack.pop();
        if(p)
        {
            if(!v)
            {
                setError("can't return a nullptr");
                return;
            }
            if(!p->set(v->getP(), v->getType()))
                setError("set(Value) error in ret(Value)");
        }
    }
    if(!call_stack.empty())
    {
        RunState& f = call_stack.top();
        pc = f.pc;
        id = f.id;
        scope = f.scope;
        ifstack = f.ifstack;
        for(auto &i: currentVars) i.clear();
        for(auto &i: currentRegs) i.clear();
        currentVars.swap(f.vars);
        currentRegs.swap(f.regs);
        call_stack.pop();
    }
}

void Script::ret(const int& v)
{
    if(return_stack.empty())
    {
        if(id != entrypoint) setError("return stack is empty");
        else state = STOP;
    }
    else
    {
        Value* p = return_stack.top();
        return_stack.pop();
        if(p)
        {
            if(!p->set(v))
                setError("set(int) error in ret(int)");
        }
    }
    if(!call_stack.empty())
    {
        RunState& f = call_stack.top();
        pc = f.pc;
        id = f.id;
        scope = f.scope;
        ifstack = f.ifstack;
        for(auto &i: currentVars) i.clear();
        for(auto &i: currentRegs) i.clear();
        currentVars.swap(f.vars);
        currentRegs.swap(f.regs);
        call_stack.pop();
    }
}

void Script::ret(const float& v)
{
    if(return_stack.empty())
    {
        if(id != entrypoint) setError("return stack is empty");
        else state = STOP;
    }
    else
    {
        Value* p = return_stack.top();
        return_stack.pop();
        if(p)
        {
            if(!p->set(v))
                setError("set(float) error in ret(float)");
        }
    }
    if(!call_stack.empty())
    {
        RunState& f = call_stack.top();
        pc = f.pc;
        id = f.id;
        scope = f.scope;
        ifstack = f.ifstack;
        for(auto &i: currentVars) i.clear();
        for(auto &i: currentRegs) i.clear();
        currentVars.swap(f.vars);
        currentRegs.swap(f.regs);
        call_stack.pop();
    }
}

void Script::ret(const std::string& v)
{
    if(return_stack.empty())
    {
        if(id != entrypoint) setError("return stack is empty");
        else state = STOP;
    }
    else
    {
        Value* p = return_stack.top();
        return_stack.pop();
        if(p)
        {
            if(!p->set(v))
                setError("set(string) error in ret(string)");
        }
    }
    if(!call_stack.empty())
    {
        RunState& f = call_stack.top();
        pc = f.pc;
        id = f.id;
        scope = f.scope;
        ifstack = f.ifstack;
        for(auto &i: currentVars) i.clear();
        for(auto &i: currentRegs) i.clear();
        currentVars.swap(f.vars);
        currentRegs.swap(f.regs);
        call_stack.pop();
    }
}

bool Script::compile(const std::string& file, const std::string& output, const char &flag)
{
    // source file
    std::ifstream f(file, std::ios::in | std::ios::binary);
    if(!f)
        return false;

    // used vars
    std::string buf; // to store strings
    bool isstr = false; // if true, we are passing a STR type
    bool escape = false; // next character is escaped
    int comment = 0; // commenting state
    char c; // to store a character

    // test
    bool isnum = false;
    bool iswd = false;
    bool isfl = false;
    bool isgvar = false;

    TokenIDList tokens; // list of tokens after parsing
    Compiled code(20); // resulting code

    // parsing the source file
    do
    {
        f.read(&c, 1);
        if(!f.good()) continue;

        // comment mode (triggered by the '/' char)
        // comments are "ignored" and won't be parsed
        if(comment == 1)
        {
            if(c == '/') // single line comment
            {
                comment = 2;
                continue;
            }
            else if(c == '*') // multi line comment
            {
                comment = 3;
                continue;
            }
            // it wasn't a comment, continue normally
            buf += '/';
            comment = 0;
        }
        else if(comment == 2) // single line
        {
            if(c == '\n') // over if we encounter the end of the line
                comment = 0;
            continue;
        }
        else if(comment > 2) // multi line (simple state machine to support nested comments)
        {
            switch((comment-3)%3)
            {
                case 0:
                    if(c == '*') comment += 1;
                    else if(c == '/') comment += 2;
                    continue;
                case 1:
                    if(c == '/') comment -= 4;
                    else if(c != '*') comment -= 1;
                    continue;
                case 2:
                    if(c == '*') comment += 1;
                    else if(c != '/') comment -= 2;
                    continue;
            }
        }

        // normal mode
        // std::regex is terribly slow so we parse the tokens manually
        // starting with non string tokens:
        if(!isstr)
        {
            if(c == '/') // either a comment or the divide operator
            {
                if(!buf.empty())
                {
                    tokens.push_back(buf);
                    buf.clear();
                }
                comment = 1;
                isnum = false;
                iswd = false;
                isfl = false;
                isgvar = false;
            }
            else if(std::isspace(c)) // a whitespace separates the tokens
            {
                if(!buf.empty())
                {
                    tokens.push_back(buf);
                    buf.clear();
                }
                buf.clear();
                isnum = false;
                iswd = false;
                isfl = false;
                isgvar = false;
            }
            else if(std::isalpha(c) || c == '_') // character or _ (it has to be part of a keyword, function or variable)
            {
                if(isnum)
                {
                    if(!buf.empty())
                    {
                        tokens.push_back(buf);
                        buf.clear();
                    }
                    buf.clear();
                    isnum = false;
                }
                else if(!iswd)
                {
                    if(!buf.empty())
                    {
                        tokens.push_back(buf);
                        buf.clear();
                    }
                }
                buf += c;
                iswd = true;
                isfl = false;
                isgvar = false;
            }
            else if(c == '@') // @ (global var name)
            {
                if(!buf.empty())
                {
                    tokens.push_back(buf);
                    buf.clear();
                }
                buf.clear();
                buf += c;
                isnum = false;
                iswd = false;
                isfl = false;
                isgvar = true;
            }
            else if(std::isdigit(c)) // digit (part of a keyword but not the first character, int or float)
            {
                if(!iswd && !isnum && !isgvar)
                {
                    if(!buf.empty())
                    {
                        tokens.push_back(buf);
                        buf.clear();
                    }
                    buf.clear();
                    isnum = true;
                    isfl = false;
                }
                buf += c;
            }
            else if(c == '.') // dot (only used in float). will trigger an error later if misused
            {
                if(!isfl && isnum)
                {
                    isfl = true;
                }
                else
                {
                    if(!buf.empty())
                    {
                        tokens.push_back(buf);
                        buf.clear();
                    }
                    buf.clear();
                    iswd = false;
                    isnum = false;
                    isfl = false;
                    isgvar = false;
                }
                buf += c;
            }
            else if(c == '"') // start of a string
            {
                if(!buf.empty())
                {
                    tokens.push_back(buf);
                    buf.clear();
                }
                buf.clear();
                iswd = false;
                isnum = false;
                isfl = false;
                isgvar = false;
                isstr = true; // enable string mode
                buf += c;
            }
            else if(c == '(' || c == '{' || c == ')' || c == '}' || c == ',' || c == ';') // various used character
            {
                if(!buf.empty())
                {
                    tokens.push_back(buf);
                    buf.clear();
                }
                buf += c;
                isnum = false;
                iswd = false;
                isfl = false;
                isgvar = false;
            }
            else if(c == '=') // equal operator
            {
                if(!buf.empty())
                {
                    tokens.push_back(buf);
                    buf.clear();
                    buf += c;

                    if(tokens.back().size() == 1) // we check if it follows directly one of these operator (example: += )
                    {
                        char d = tokens.back()[0];
                        if(d == '+' || d == '-' || d == '*' || d == '/' || d == '%' || d == '<' || d == '=' || d == '>' || d == '!')
                        {
                            tokens.back() += c; // we concatenate if it's the case
                            buf.clear();
                        }
                    }
                }
                else
                {
                    buf += c;
                }
                isnum = false;
                iswd = false;
                isfl = false;
                isgvar = false;
            }
            else if(c == '+' || c == '-' || c == '&' || c == '^' || c == '|') // operators which can be doubled (example: ++ )
            {
                if(!buf.empty())
                {
                    tokens.push_back(buf);
                    buf.clear();
                    buf += c;
                    if(tokens.back() == buf)
                    {
                        tokens.back() += c;
                        buf.clear();
                    }
                }
                else
                {
                    buf += c;
                }
                isnum = false;
                iswd = false;
                isfl = false;
                isgvar = false;
            }
            else /*if(c == '<' || c == '>' || c == '*' || c == '%' || c == '!')*/ // other operators + any unexpected chars (those will trigger an error)
            {
                if(!buf.empty())
                {
                    tokens.push_back(buf);
                    buf.clear();
                }
                buf += c;
                isnum = false;
                iswd = false;
                isfl = false;
                isgvar = false;
            }
        }
        else // string mode
        {
            if(c == '\\' && !escape) // escape mode
            {
                escape = true;
            }
            else if(c == '"' && !escape) // end of string
            {
                isstr = false;
                buf += c; // we keep the ", we use it later to check if the token is a string
                if(!buf.empty())
                    tokens.push_back(buf);
                buf.clear();
            }
            else if(c == '\r') // used by windows, we skip
            {
                continue;
            }
            else if(c == '\n' && !escape) // unescaped end of line, we trigger an error for later, on purpose
            {
                if(!buf.empty())
                    tokens.push_back(buf);
                buf.clear();
                isstr = false;
            }
            else // everything else is saved
            {
                buf += c;
                escape = false;
            }
        }
    }while(f.good());

    if(comment != 0 && comment != 2)
    {
        std::cout << "missing a '*/' ?" << std::endl;
        return false;
    }

    if(!buf.empty())
        tokens.push_back(buf);

    bool err = !shuntingyard(tokens, code); // apply a shunting yard algorithm (+ the formatting, error check and optimization)

    // check for anything weird
    if(!err)
    {
        switch(errorCheck(code))
        {
            case 0: break;
            case 1: std::cout << "op isn't of type FUNC or OPERATOR" << std::endl; err = true; break;
            case 2: std::cout << "parameter must be a value or variable" << std::endl; err = true; break;
            case 3: std::cout << "unknown function call" << std::endl; err = true; break;
            case 4: std::cout << "mismatched number of parameters" << std::endl; err = true; break;
        }
    }

    if(!err && !postprocessing(code))
    {
        err = true;
        std::cout << "postprocessing failed" << std::endl;
    }

    if(!err && (flag & PRINT))
        print(code);

    if(!err && !save(output, code))
    {
        err = true;
        std::cout << "writing to " << output << " failed" << std::endl;
    }

    // free memory
    for(auto &xi: code)
    {
        for(auto &xj: xi.second.line)
        {
            delete xj.op;
            for(auto &xk: xj.params)
                delete xk;
        }
    }

    return !err;
}

bool Script::shuntingyard(const TokenIDList& tokens, Compiled& code)
{
    // vars
    Program prog(20); // will contain the code in RPN
    VariableList vars(20); // variable names used by the code
    bool ret; // return value

    std::stack<Token*> stack; // operator stack
    std::vector<Token*> output; // output stack
    // default scope is the main function, named with an empty string
    prog[""];
    vars[""];
    code[""];
    std::string last_def = ""; // name of the function we are in
    size_t scp = 0; // scope (to use with { } )
    Token *tk = nullptr; // to store a token pointer
    std::vector<std::string>::const_iterator it = tokens.cbegin(); // just pointing to the start of tokens: to not be modified

    // #####################
    // state machine start
    // #####################

line_start: // start of a new "line"
    if(it == tokens.cend())
    {
        if(last_def != "") goto sy_error; // if we are in a function, error
        else goto ended; // we are done
    }
    if(*it == "def") // it's a new function definition
    {
        if(last_def != "" || scp != 0) goto sy_error;
        ++it;
        goto function_def;
    }
    else if(*it == "}") // it's the end of a definition or of a condition block
    {
        if(scp == 0)
        {
            if(last_def == "") goto sy_error;
            last_def = "";
        }
        else
        {
            prog[last_def].push_back({new Token(*it, RCUR)});
            --scp;
            ++it;
            goto line_start;
        }
        ++it;
        goto line_start;
    }
    else if(*it == ";") // empty line with just a ";", we skip
    {
        ++it;
        goto line_start;
    }
    // else, continue too want_operand
want_operand:
    if(it == tokens.cend()) goto sy_error; // we shouldn't reach the end here
    if(*it == "(" || isPrefixOp(*it)) // kinda explicit
    {
        stack.push(new Token(*it, (*it == "(") ? LBRK : OPERATOR, PREFIX)); // push to the stack
        ++it;
        goto want_operand;
    }
    else if(*it == ")") // closing bracket, no arg function
    {
        #warning ":("
        if(stack.empty()) goto sy_empty_stack;
        tk = stack.top();
        if(tk->t != LBRK) goto sy_error;
        delete tk;
        stack.pop();
        ++it;
        goto have_operand;
    }
    else if(*it == "def") goto sy_error; // def keyword is forbidden, can't be a function name
    else
    {
        switch(detectType(*it)) // check what we got and push to the output
        {
            case 0: output.push_back(new Token(*it, STR)); break;
            case 1: output.push_back(new Token(*it, INT)); break;
            case 2: output.push_back(new Token(*it, FLOAT)); break;
            case 3:
                if(!isFunction(*it, code)) // var
                {
                    output.push_back(new Token(*it, VAR));
                    if(vars[last_def].find(*it) == vars[last_def].end())
                        vars[last_def].insert(*it);
                }
                else
                {
                    stack.push(new Token(*it, FUNC)); // function call counts as operators so they are sent to the stack
                    if(*it == "else") // "else" function is a bit special, we don't want (arg1, ..., argN) after
                    {
                        ++it;
                        goto have_operand;
                    }
                    ++it;
                    goto want_operand;
                }
                break;
            case 4:
            {
                std::string buf = it->substr(1);
                if(std::stoul(buf) >= globalVars.size())
                    goto sy_gvar_error;
                output.push_back(new Token(buf, GVAR));
                break;
            }
            default:
                goto sy_error; // anything else is an error
        }

        ++it;
        goto have_operand;
    }
function_def: // definition of a new function
    {
        if(it == tokens.cend()) goto sy_error; // eof
        if(!isName(*it) || isFunction(*it, code) || *it == "def") // check if the function name is valid
            goto sy_def_error;
        code[*it];
        auto &cdef = code[*it].argn; // register it with a parameter count of zero (for now)
        prog[*it]; // create the needed stuff
        auto &cvars = vars[*it] = std::set<std::string>();
        last_def = *it; // update last_def

        if(++it == tokens.cend()) goto sy_error;
        if(*it != "(") goto sy_def_error; // expect (

        if(++it == tokens.cend()) goto sy_error;

        if(*it == ")") goto def_end; // if ), we are already done

        def_args:
            if(isName(*it)) // expect a var name
            {
                ++cdef;
                if(cvars.find(*it) != cvars.end())
                    goto sy_def_error;
                cvars.insert(*it); // register it (name must be unused in the function scope)
            }
            else goto sy_def_error; // else error
            if(++it == tokens.cend()) goto sy_error;

            // next, expect either a comma or a closing bracket
            if(*it == ")") goto def_end; // ) means we are done
            else if(*it != ",") goto sy_def_error;
            // comma means we expect another parameter
            if(++it == tokens.cend()) goto sy_error;
            goto def_args; // back to the start
        def_end:

        if(++it == tokens.cend()) goto sy_error;
        if(*it != "{") goto sy_def_error; // next, we expect a block start

        ++it;
        goto line_start; // back to the start
    }
have_operand: // explicit
    if(it == tokens.cend()) // valid way to reach the end of the file
    {
        while(!stack.empty()) // empty the stack
        {
            tk = stack.top();
            if(tk->t == LBRK) // while checking for erros
            {
                goto sy_error;
            }
            stack.pop();
            output.push_back(tk);
        }
    }
    else if(isPostfixOp(*it)) // ++, --, etc
    {
        if(!output.empty())
        {
            tk = output.back();
            if(tk->t == VAR)
            {
                output.push_back(new Token(*it, OPERATOR, POSTFIX));
                ++it;
                goto have_operand;
            }
        }
        stack.push(new Token(*it, OPERATOR, PREFIX));
        ++it;
        goto want_operand;
    }
    else if(*it == ")") // closing bracket
    {
        if(stack.empty()) goto sy_empty_stack;
        tk = stack.top();
        while(tk->t != LBRK)
        {
            output.push_back(tk);
            stack.pop();
            if(stack.empty()) goto sy_empty_stack;
            tk = stack.top();
        }
        delete tk;
        stack.pop();
        ++it;
        goto have_operand;
    }
    else if(*it == ",") // comma (in function calls)
    {
        if(stack.empty()) goto sy_empty_stack;
        tk = stack.top();
        while(tk->t != LBRK)
        {
            output.push_back(tk);
            stack.pop();
            if(stack.empty()) goto sy_empty_stack;
            tk = stack.top();
        }
        ++it;
        goto want_operand;
    }
    else if(isInfixOp(*it)) // +, -, =, etc
    {
        if(!stack.empty())
            tk = stack.top();
        while(!stack.empty() && (tk->t == FUNC || (tk->t == OPERATOR && (greater_precedence(tk->s, *it) || (equal_precedence(tk->s, *it) && tk->o == PREFIX)) && *it != "^")) && tk->t != LBRK)
        {
            stack.pop();
            output.push_back(tk);
            if(!stack.empty())
                tk = stack.top();
        }
        stack.push(new Token(*it, OPERATOR, INFIX));
        ++it;
        goto want_operand;
    }
    else if(*it == "{") // condition block
    {
        if(stack.empty()) goto sy_error;
        tk = stack.top();
        if(!isCondition(tk->s)) goto sy_error;
        while(!stack.empty())
        {
            tk = stack.top();
            stack.pop();
            output.push_back(tk);
        }
        prog[last_def].push_back(output);
        output.clear();

        prog[last_def].push_back({new Token(*it, LCUR)});

        ++scp;
        ++it;
        goto line_start;
    }
    else if(*it == ";") // end of "line"
    {
        ++it;
        while(!stack.empty()) // we empty
        {
            tk = stack.top();
            if(tk->t == LBRK)
            {
                goto sy_error;
            }
            stack.pop();
            output.push_back(tk);
        }
        prog[last_def].push_back(output); // and create a new line
        output.clear();
        goto line_start;
    }
    else goto sy_error;

ended:
    //we are done
    //debug(prog);
    if(!format(prog, vars, code)) // convert RPN to something easier to process
    {
        std::cout << "Conversion error" << std::endl;
        goto sy_pp_error;
    }

    ret = true;
    goto sy_end;

    // error messages
sy_def_error:
    std::cout << "def error" << std::endl;
    goto sy_end_error;

sy_empty_stack:
    std::cout << "stack error" << std::endl;
    goto sy_end_error;

sy_pp_error:
    std::cout << "post compilation error" << std::endl;
    goto sy_end_error;

sy_gvar_error:
    std::cout << "invalid global variable id" << std::endl;
    goto sy_end_error;

sy_error:
    std::cout << "unknown error" << std::endl;
    std::cout << std::endl;
    std::cout << "@" << last_def << std::endl;
    debug(prog);
    goto sy_end_error;

sy_end_error:
    ret = false;
sy_end:
    // free the memory
    while(!stack.empty())
    {
        delete stack.top();
        stack.pop();
    }
    for(auto& o: output)
        delete o;
    for(auto& i: prog)
        for(auto& j: i.second)
            for(auto& k: j)
                delete k;
    return ret;
}

bool Script::format(Program &prog, VariableList &vars, Compiled& code)
{
    std::vector<Instruction> postfixes;
    for(auto &xi: prog)
    {
        for(auto xj: vars[xi.first])
            code[xi.first].var.push_back(xj);
        vars[xi.first].clear();

        size_t j;
        for(auto &xj: xi.second)
        {
            // if markers { and } are directly processed
            if(xj.size() == 1 && (xj[0]->t == LCUR || xj[0]->t == RCUR))
            {
                Instruction ins;
                ins.op = xj[0];
                code[xi.first].line.push_back(ins);
                xj.clear();
                continue;
            }
            // and single token lines are ignored (excluding functions)
            else if(xj.size() == 1 && xj[0]->t != FUNC)
            {
                continue;
            }
            // processing the RPN line
            #warning "maybe switch to reverse order later"
            std::vector<bool> regs; // track temporary variable uses
            for(size_t i = 0; i < xj.size(); ++i) // go through tokens
            {
                // until we find an operator or function call
                if(xj[i]->t == OPERATOR)
                {
                    if(isSingleOp(xj[i]->s) || (xj[i]->s == "-" && xj[i]->o == PREFIX))
                        j = i - 1;
                    else j = i - 2;
                }
                else if(xj[i]->t == FUNC)
                {
                    auto ast = code.find(xj[i]->s);
                    if(ast != code.end())
                        j = i - ast->second.argn;
                    else
                    {
                        auto bst = gl_func.find(xj[i]->s);
                        if(bst != gl_func.end())
                            j = i - bst->second;
                        else goto fc_misf_error;
                    }
                }
                else continue; // else, next token
                if(j < 0) goto fc_argn_error;

                // j contains the position of the first parameter needed by the function/operator

                // create the instruction
                Instruction ins;
                ins.op = xj[i]; // function/operator

                switch(ins.op->o)
                {
                    case POSTFIX: // ++ --
                        if(j != i - 1) goto fc_error;
                        switch(xj[j]->t)
                        {
                            case RESULT:
                                regs[xj[j]->getInt()] = false; // nobreak
                            case INT: case FLOAT: case STR: case VAR: case GVAR:
                                ins.params.push_back(new Token(*xj[j]));
                                break;
                            default:
                                goto fc_para_error;
                                break;
                        }
                        postfixes.push_back(ins);
                        xj.erase(xj.begin()+i); // remove the used tokens from the RPN lines
                        --i;
                        break;
                    case PREFIX: // - ! ++ --
                        if(j != i - 1) goto fc_argn_error;
                        switch(xj[j]->t)
                        {
                            case RESULT:
                                regs[xj[j]->getInt()] = false; // nobreak
                            case INT: case FLOAT: case STR: case VAR: case GVAR:
                                ins.params.push_back(new Token(*xj[j]));
                                break;
                            default:
                                goto fc_para_error;
                                break;
                        }
                        switch(op_unordered_map.at(ins.op->s))
                        {
                            case 5: case 2: // ! -
                            {
                                // search a free tmp variable (to store the result)
                                size_t r = 0;
                                for(; r < regs.size(); ++r)
                                    if(regs[r] == false)
                                        break;
                                if(r == regs.size()) // create a new one if none
                                    regs.push_back(false);
                                regs[r] = true; // mark the temp variable as non free
                                ins.params.push_back(new Token(std::to_string(r), RESULT)); // add the temp variable as an extra parameter
                                ins.hasResult = true;
                                code[xi.first].line.push_back(ins); // store the instruction
                                xj.erase(xj.begin()+j, xj.begin()+i); // remove the used tokens from the RPN lines
                                xj[j] = new Token(std::to_string(r), RESULT); // place the temp variable where the used tokens were
                                i = j;
                                break;
                            }
                            case 18: case 19: // ++ --
                                code[xi.first].line.push_back(ins);
                                xj.erase(xj.begin()+i); // remove the used tokens from the RPN lines
                                --i;
                                break;
                            default: ins.op = nullptr; ins.clear(); goto fc_op_error;
                        }
                        break;
                    default: // everything else
                    {
                        if(j >= 0) // store the parameters if any
                            for(size_t k = j; k < i; ++k)
                            {
                                switch(xj[k]->t)
                                {
                                    case RESULT:
                                        regs[xj[k]->getInt()] = false; // nobreak
                                    case INT: case FLOAT: case STR: case VAR: case GVAR:
                                        ins.params.push_back(xj[k]);
                                        break;
                                    default:
                                        goto fc_para_error;
                                        break;
                                }
                            }
                        // search a free tmp variable (to store the result)
                        size_t r = 0;
                        for(; r < regs.size(); ++r)
                            if(regs[r] == false)
                                break;
                        if(r == regs.size()) // create a new one if none
                            regs.push_back(false);

                        ins.params.push_back(new Token(std::to_string(r), RESULT)); // add the temp variable as an extra parameter
                        ins.hasResult = true;
                        regs[r] = true; // mark the temp variable as non free
                        code[xi.first].line.push_back(ins); // store the instruction
                        xj.erase(xj.begin()+j, xj.begin()+i); // remove the used tokens from the RPN lines
                        xj[j] = new Token(std::to_string(r), RESULT); // place the temp variable where the used tokens were
                        i = j;
                        break;
                    }
                }
            }
            if(!code[xi.first].line.empty() && code[xi.first].line.back().hasResult)
            {
                code[xi.first].line.back().params.pop_back();
                code[xi.first].line.back().hasResult = false;
            }
            if(!postfixes.empty())
            {
                do
                {
                    code[xi.first].line.push_back(postfixes[0]);
                    postfixes.erase(postfixes.begin());
                }while(!postfixes.empty());
            }
            else if(xj.size() != 1 || xj[0]->t != RESULT)
                goto fc_end_error;
        }
    }
    return true;

fc_argn_error:
    std::cout << "number of parameters doesn't match the function definition" << std::endl;
    goto fc_error;
fc_para_error:
    std::cout << "unexpected parameter type, the problem is either an incorrect number of parameters or a compiler issue" << std::endl;
    goto fc_error;
fc_op_error:
    std::cout << "unexpected operator, compiler issue" << std::endl;
    goto fc_error;
fc_misf_error:
    std::cout << "unknown function" << std::endl;
    goto fc_error;
fc_end_error:
    std::cout << "unexpected code end" << std::endl;
    goto fc_error;
fc_error:
    for(auto &xi: code)
    {
        for(auto &xj: xi.second.line)
            xj.clear();
    }
    code.clear();
    for(auto &xi: postfixes)
    {
        xi.clear();
    }
    return false;
}

int Script::errorCheck(Compiled& code)
{
    for(auto &xi: code)
    {
        Code& func = xi.second;
        for(auto &xj: func.line)
        {
            switch(xj.op->t)
            {
                case FUNC:
                {
                    size_t p;
                    auto ast = code.find(xj.op->s);
                    if(ast != code.end())
                        p = ast->second.argn;
                    else
                    {
                        auto bst = gl_func.find(xj.op->s);
                        if(bst != gl_func.end())
                            p = bst->second;
                        else return 3;
                    }

                    if(xj.hasResult)
                        p++;
                    if(p != xj.params.size())
                        return 4;
                }
                case OPERATOR: case LCUR: case RCUR:
                    break;
                default:
                    return 1;
            }
            for(auto &xk: xj.params)
            {
                switch(xk->t)
                {
                    case FUNC: case OPERATOR: case LCUR: case RCUR:
                        return 2;
                    default:
                        break;
                }
            }
        }
    }
    return 0;
}

bool Script::postprocessing(Compiled& code)
{
    for(auto &xi: code)
    {
        std::vector<Instruction>& ins = xi.second.line;
        std::vector<std::pair<Token*, Token*> > replace;
        size_t c;
        size_t ri;
        for(int i = 0; i < (int)ins.size(); ++i)
        {
            // Instruction()
            switch(ins[i].op->t)
            {
                case FUNC:
                    if(code.find(ins[i].op->s) != code.end())
                        c = code[ins[i].op->s].argn;
                    else if(gl_func.find(ins[i].op->s) != gl_func.end())
                        c = gl_func[ins[i].op->s];
                    break;
                case OPERATOR:
                    if(isSingleOp(ins[i].op->s) || (ins[i].op->s == "-" && ins[i].op->o == PREFIX))
                        c = 1;
                    else c = 2;
                    break;
                case LCUR: case RCUR:
                    continue;
                default:
                    return false;
            }
            if((ins[i].hasResult && ins[i].params.size() != c + 1) ||
               (!ins[i].hasResult && ins[i].params.size() != c))
                return false;

            for(size_t j = 0; j < c; ++j)
            {
                for(ri = 0; ri < replace.size(); ++ri)
                    if(*(replace[ri].first) == *(ins[i].params[j]))
                        break;
                if(ri != replace.size())
                {
                    delete ins[i].params[j];
                    ins[i].params[j] = new Token(*(replace[ri].second));
                    delete replace[ri].first;
                    delete replace[ri].second;
                    replace.erase(replace.begin()+ri);
                }
            }
            if(ins[i].hasResult)
            {
                for(ri = 0; ri < replace.size(); ++ri)
                    if(*(replace[ri].first) == *(ins[i].params[c]))
                        break;
                if(ri != replace.size())
                {
                    delete replace[ri].first;
                    delete replace[ri].second;
                    replace.erase(replace.begin()+ri);
                }
            }

            switch(ins[i].op->t)
            {
                case OPERATOR:
                    if(ins[i].op->s == "=")
                    {
                        int pzt = ins[i].params[0]->t;
                        if(!ins[i].hasResult && (pzt != VAR && pzt != RESULT && pzt != GVAR))
                        {
                            ins[i].clear();
                            ins.erase(ins.begin()+i);
                            break;
                        }
                        else if(ins[i].hasResult && (pzt == VAR || pzt == RESULT || pzt == GVAR))
                        {
                            replace.push_back({ins[i].params[2], new Token(*(ins[i].params[0]))});
                            ins[i].params.pop_back();
                            ins[i].hasResult = false;
                        }
                        if(!ins[i].hasResult && (pzt == VAR || pzt == RESULT || pzt == GVAR) && ins[i].params[1]->t == RESULT)
                        {
                            for(int j = i - 1; j >= 0; --j)
                            {
                                if(ins[j].hasResult && *(ins[j].params.back()) == *(ins[i].params[1]))
                                {
                                    delete ins[j].params[ins[j].params.size()-1];
                                    ins[j].params[ins[j].params.size()-1] = ins[i].params[0];
                                    ins[i].params[0] = nullptr;
                                    ins[i].clear();
                                    ins.erase(ins.begin()+i);
                                    i--;
                                    break;
                                }

                            }
                        }
                    }
                    else if(ins[i].op->s == "-" && ins[i].op->o == PREFIX)
                    {
                        if(ins[i].hasResult)
                        {
                            if(ins[i].params[0]->isNumber())
                            {
                                ins[i].params[0]->inverseSign();
                                replace.push_back({ins[i].params[1], ins[i].params[0]});
                                ins[i].params.clear();
                                ins[i].clear();
                                ins.erase(ins.begin()+i);
                                i--;
                            }
                        }
                        else
                        {
                            ins[i].clear();
                            ins.erase(ins.begin()+i);
                            i--;
                        }
                    }
                    else // +=, -=, etc... NOT != and ==
                    {
                        std::string tmp = ins[i].op->s;
                        if(tmp.size() == 2 && tmp[1] == '=' && tmp[0] != '!' && tmp[0] != '=' && tmp[0] != '>' && tmp[0] != '<')
                        {
                            int pzt = ins[i].params[0]->t;
                            if(ins[i].hasResult && (pzt == VAR || pzt == RESULT || pzt == GVAR))
                            {
                                replace.push_back({ins[i].params[2], new Token(*(ins[i].params[0]))});
                                ins[i].params.pop_back();
                                ins[i].hasResult = false;
                            }
                        }
                    }
                    break;
                default:
                    continue;
            }
        }
        for(auto r: replace)
            delete r.first;
    }
    for(auto &xi: code)
    {
        Code& func = xi.second;
        func.creg = 0;
        for(auto &xj: func.line)
        {
            switch(xj.op->t)
            {
                case OPERATOR:
                {
                    Token *tmp = new Token(std::to_string(op_unordered_map.at(xj.op->s)), COP);
                    delete xj.op;
                    xj.op = tmp;
                    break;
                }
                default:
                    break;
            }
            for(auto &xk: xj.params)
            {
                switch(xk->t)
                {
                    case VAR:
                    {
                        size_t id = 0;
                        for(; id < func.var.size(); ++id)
                        {
                            if(func.var[id] == xk->s)
                                break;
                        }
                        if(id != func.var.size())
                        {
                            delete xk;
                            xk = new Token(std::to_string(id), CVAR);
                        }
                        break;
                    }
                    case RESULT:
                        if(xk->getInt()+1 > (int)func.creg)
                            func.creg = xk->getInt()+1;
                        break;
                }
            }
        }
    }
    return true;
}

bool Script::save(const std::string& output, const Compiled& code)
{
    // tmp vars used during the writing
    size_t tmp;
    float fv;
    std::string buf;

    // saving the result
    // (file format subject to change)
    // output file
    std::ofstream o(output, std::ios::out | std::ios::trunc | std::ios::binary);
    if(!o)
        return false;
    tmp = SCRIPT_MAGIC;
    o.write((char*)&tmp, 4);
    tmp = code.size();
    o.write((char*)&tmp, 4);
    for(auto &xi: code)
    {
        tmp = xi.first.size();
        o.write((char*)&tmp, 4);
        if(tmp) o.write(xi.first.c_str(), tmp);
        tmp = xi.second.argn;
        o.write((char*)&tmp, 4);
    }
    for(auto &xi: code)
    {
        const Code& func = xi.second;

        tmp = func.creg;
        o.write((char*)&tmp, 4);
        tmp = func.var.size();
        o.write((char*)&tmp, 4);
        tmp = func.line.size();
        o.write((char*)&tmp, 4);

        for(auto &xj: func.line)
        {
            tmp = xj.op->t;
            o.write((char*)&tmp, 1);
            if(xj.op->isIntValue())
            {
                tmp = xj.op->getInt();
                o.write((char*)&tmp, 4);
            }
            else if(xj.op->isFloatValue())
            {
                fv = xj.op->getFloat();
                o.write((char*)&fv, 4);
            }
            else
            {
                if(xj.op->isStringValue()) buf = xj.op->getStrippedString();
                else buf = xj.op->s;
                tmp = buf.size();
                o.write((char*)&tmp, 4);
                if(tmp) o.write(buf.c_str(), tmp);
            }

            o.write((char*)&(xj.hasResult), 1);
            tmp = xj.params.size();
            o.write((char*)&tmp, 4);

            for(auto &xk: xj.params)
            {
                tmp = xk->t;
                o.write((char*)&tmp, 1);
                if(xk->isIntValue())
                {
                    tmp = xk->getInt();
                    o.write((char*)&tmp, 4);
                }
                else if(xk->isFloatValue())
                {
                    fv = xk->getFloat();
                    o.write((char*)&fv, 4);
                }
                else
                {
                    if(xk->isStringValue()) buf = xk->getStrippedString();
                    else buf = xk->s;
                    tmp = buf.size();
                    o.write((char*)&tmp, 4);
                    if(tmp) o.write(buf.c_str(), tmp);
                }
            }
        }
    }
    return true;
}

void Script::print(Compiled& code)
{
    for(auto &xi: code)
    {
        if(xi.first == "")
            std::cout << "# main" << std::endl;
        else
            std::cout << "# function: " << xi.first << std::endl;
        std::cout << "# variable count: " << code[xi.first].var.size() << std::endl;
        std::cout << "# register count: " << code[xi.first].creg << std::endl;
        size_t xl = 0;
        for(auto xj: code[xi.first].line)
        {
            std::cout << xl << " -> ";
            ++xl;
            if(xj.op->t == RESULT) std::cout << "r" << xj.op->getInt() << " ";
            else if(xj.op->t == GVAR) std::cout << "@" << xj.op->getInt() << " ";
            else if(xj.op->t == CVAR) std::cout << "v" << xj.op->getInt() << " ";
            else if(xj.op->isIntValue()) std::cout << xj.op->getInt() << " ";
            else if(xj.op->isFloatValue()) std::cout << xj.op->getFloat() << " ";
            else std::cout << xj.op->s << " ";
            std::cout << (xj.hasResult ? "[1] " : "[0] ");
            for(auto j: xj.params)
            {
                if(j->t == RESULT) std::cout << "r" << j->getInt() << " ";
                else if(j->t == GVAR) std::cout << "@" << j->getInt() << " ";
                else if(j->t == CVAR) std::cout << "v" << j->getInt() << " ";
                else if(j->isIntValue()) std::cout << j->getInt() << " ";
                else if(j->isFloatValue()) std::cout << j->getFloat() << " ";
                else std::cout << j->s << " ";
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }
}

void Script::debug(Program& code)
{
    for(auto &i: code)
    {
        std::cout << "### " << i.first << std::endl;
        for(auto &j: i.second)
        {
            for(auto &k: j)
            {
                if(k->t == RESULT) std::cout << "r" << k->getInt() << " ";
                else if(k->t == RESULT) std::cout << "@" << k->getInt() << " ";
                else if(k->t == CVAR) std::cout << "v" << k->getInt() << " ";
                else if(k->isIntValue()) std::cout << k->getInt() << " ";
                else if(k->isFloatValue()) std::cout << k->getFloat() << " ";
                else std::cout << k->s << " ";
            }
            std::cout << std::endl;
        }
    }
}

void Script::operation(Line& line)
{
    int op_id = *(line.op.get<int>());
    int n = 0;
    const int *target;
    int ttype;
    bool equal = false;

    const void *u[2];
    int t[2];
    Value* V[2];

    switch(op_id)
    {
        case 0:
            n = 1;
            equal = true;
            target = line.params[0].get<int>();
            ttype = line.params[0].getType();
            V[0] = &line.params[1];
            break;
        case 2:// "-"
            n = line.params.size() - (line.hasResult ? 1 : 0);
            if(!line.hasResult) return;
            target = line.params.back().get<int>();
            ttype = line.params.back().getType();
            if(n == 1)
            {
                equal = true;
                V[0] = &line.params[0];
            }
            else
            {
                V[0] = &line.params[0];
                V[1] = &line.params[1];
            }
            break;
        case 1: case 3: case 4: case 6: case 7: case 8: case 9: case 10: case 11: case 12:  case 13:  case 14:  case 15:
        case 16: case 17: case 24:
            n = 2;
            if(!line.hasResult) return;
            target = line.params.back().get<int>();
            ttype = line.params.back().getType();
            V[0] = &line.params[0];
            V[1] = &line.params[1];
            break;
        case 20: case 21: case 22: case 23: case 25:
            n = 2;
            target = line.params[0].get<int>();
            ttype = line.params[0].getType();
            V[0] = &line.params[0];
            V[1] = &line.params[1];
            break;
        case 5:
            n = 1;
            if(!line.hasResult) return;
            target = line.params.back().get<int>();
            ttype = line.params.back().getType();
            V[0] = &line.params[0];
            break;
        case 18: case 19:
            n = 1;
            if(line.hasResult)
            {
                target = line.params.back().get<int>();
                ttype = line.params.back().getType();
            }
            else
            {
                target = line.params[0].get<int>();
                ttype = line.params[0].getType();
            }
            V[0] = &line.params[0];
            break;
        default:
            goto op_ins_error;
    }

    for(int i = 0; i < n; ++i)
        u[i] = getValueContent(*V[i], t[i]);

    switch(op_id)
    {
        case 0:
            switch(t[0])
            {
                case INT: setVar(*target, *(const int*)u[0], ttype); break;
                case FLOAT: setVar(*target, *(const float*)u[0], ttype); break;
                case STR: setVar(*target, *(const std::string*)u[0], ttype); break;
                default: goto op_ins_error;
            }
            break;
        case 1: case 20: // + +=
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const int*)u[0] + *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const int*)u[0] + *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, std::to_string(*(const int*)u[0]) + *(const std::string*)u[1], ttype); break;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const float*)u[0] + *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const float*)u[0] + *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, std::to_string(*(const float*)u[0]) + *(const std::string*)u[1], ttype); break;
                    }
                    break;
                case STR:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const std::string*)u[0] + std::to_string(*(const int*)u[1]), ttype); break;
                        case FLOAT: setVar(*target, *(const std::string*)u[0] + std::to_string(*(const float*)u[1]), ttype); break;
                        case STR: setVar(*target, *(const std::string*)u[0] + *(const std::string*)u[1], ttype); break;
                    }
                    break;
            }
            break;
        case 2: case 21: // - +=
            if(equal)
            {
                switch(t[0])
                {
                    case INT: setVar(*target, - *(const int*)u[0], ttype); break;
                    case FLOAT: setVar(*target, - *(const float*)u[0], ttype); break;
                    case STR: goto op_ins_error;
                    default: goto op_ins_error;
                }
            }
            else
            {
                switch(t[0])
                {
                    case INT:
                        switch(t[1])
                        {
                            case INT: setVar(*target, *(const int*)u[0] - *(const int*)u[1], ttype); break;
                            case FLOAT: setVar(*target, *(const int*)u[0] - *(const float*)u[1], ttype); break;
                            case STR: goto op_ins_error;
                        }
                        break;
                    case FLOAT:
                        switch(t[1])
                        {
                            case INT: setVar(*target, *(const float*)u[0] - *(const int*)u[1], ttype); break;
                            case FLOAT: setVar(*target, *(const float*)u[0] - *(const float*)u[1], ttype); break;
                            case STR: goto op_ins_error;
                        }
                        break;
                    case STR:  goto op_ins_error;
                }
            }
            break;
        case 3: case 22: // * *=
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const int*)u[0] * *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const int*)u[0] * *(const float*)u[1], ttype); break;
                        case STR: goto op_ins_error;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const float*)u[0] * *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const float*)u[0] * *(const float*)u[1], ttype); break;
                        case STR: goto op_ins_error;
                    }
                    break;
                case STR:  goto op_ins_error;
            }
            break;
        case 4: case 23: // / /=
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: if(*(const int*)u[1] == 0) goto op_div_error; setVar(*target, *(const int*)u[0] / *(const int*)u[1], ttype); break;
                        case FLOAT: if(*(const float*)u[1] == 0.f) goto op_div_error; setVar(*target, *(const int*)u[0] / *(const float*)u[1], ttype); break;
                        case STR: goto op_ins_error;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: if(*(const int*)u[1] == 0) goto op_div_error; setVar(*target, *(const float*)u[0] / *(const int*)u[1], ttype); break;
                        case FLOAT: if(*(const float*)u[1] == 0.f) goto op_div_error; setVar(*target, *(const float*)u[0] / *(const float*)u[1], ttype); break;
                        case STR: goto op_ins_error;
                    }
                    break;
                case STR:  goto op_ins_error;
            }
            break;
        case 24: case 25: // % %=
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: if(*(const int*)u[1] == 0) goto op_div_error; setVar(*target, *(const int*)u[0] % *(const int*)u[1], ttype); break;
                        case FLOAT: goto op_ins_error;
                        case STR: goto op_ins_error;
                    }
                    break;
                case FLOAT: goto op_ins_error;
                case STR:  goto op_ins_error;
            }
            break;
        case 5: // !
            switch(t[0])
            {
                case INT: setVar(*target, (*(const int*)u[0] == 0), ttype); break;
                case FLOAT: setVar(*target, (*(const float*)u[0] == 0.f), ttype); break;
                case STR: setVar(*target, ((const std::string*)u[0])->empty(), ttype); break;
                default: goto op_ins_error;
            }
            break;
        case 6: // !=
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const int*)u[0] != *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const int*)u[0] != *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const float*)u[0] != *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const float*)u[0] != *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case STR:
                    switch(t[1])
                    {
                        case INT: setVar(*target, 0, ttype); break;
                        case FLOAT: setVar(*target, 0, ttype); break;
                        case STR: setVar(*target, *(const std::string*)u[0] != *(const std::string*)u[1], ttype); break;
                    }
                    break;
            }
            break;
        case 7: // >
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const int*)u[0] > *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const int*)u[0] > *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const float*)u[0] > *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const float*)u[0] > *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case STR:
                    switch(t[1])
                    {
                        case INT: setVar(*target, 0, ttype); break;
                        case FLOAT: setVar(*target, 0, ttype); break;
                        case STR: setVar(*target, *(const std::string*)u[0] > *(const std::string*)u[1], ttype); break;
                    }
                    break;
            }
            break;
        case 8: // <
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const int*)u[0] < *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const int*)u[0] < *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const float*)u[0] < *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const float*)u[0] < *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case STR:
                    switch(t[1])
                    {
                        case INT: setVar(*target, 0, ttype); break;
                        case FLOAT: setVar(*target, 0, ttype); break;
                        case STR: setVar(*target, *(const std::string*)u[0] < *(const std::string*)u[1], ttype); break;
                    }
                    break;
            }
            break;
        case 9: // >=
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const int*)u[0] >= *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const int*)u[0] >= *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const float*)u[0] >= *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const float*)u[0] >= *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case STR:
                    switch(t[1])
                    {
                        case INT: setVar(*target, 0, ttype); break;
                        case FLOAT: setVar(*target, 0, ttype); break;
                        case STR: setVar(*target, *(const std::string*)u[0] >= *(const std::string*)u[1], ttype); break;
                    }
                    break;
            }
            break;
        case 10: // <=
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const int*)u[0] <= *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const int*)u[0] <= *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const float*)u[0] <= *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const float*)u[0] <= *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case STR:
                    switch(t[1])
                    {
                        case INT: setVar(*target, 0, ttype); break;
                        case FLOAT: setVar(*target, 0, ttype); break;
                        case STR: setVar(*target, *(const std::string*)u[0] <= *(const std::string*)u[1], ttype); break;
                    }
                    break;
            }
            break;
        case 11: // ==
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const int*)u[0] == *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const int*)u[0] == *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const float*)u[0] == *(const int*)u[1], ttype); break;
                        case FLOAT: setVar(*target, *(const float*)u[0] == *(const float*)u[1], ttype); break;
                        case STR: setVar(*target, 0, ttype); break;
                    }
                    break;
                case STR:
                    switch(t[1])
                    {
                        case INT: setVar(*target, 0, ttype); break;
                        case FLOAT: setVar(*target, 0, ttype); break;
                        case STR: setVar(*target, *(const std::string*)u[0] == *(const std::string*)u[1], ttype); break;
                    }
                    break;
            }
            break;
        case 12: // &
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const int*)u[0] & *(const int*)u[1], ttype); break;
                        case FLOAT: goto op_ins_error;
                        case STR: goto op_ins_error;
                    }
                    break;
                case FLOAT:  goto op_ins_error;
                case STR: goto op_ins_error;
            }
            break;
        case 13: // ^
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const int*)u[0] ^ *(const int*)u[1], ttype); break;
                        case FLOAT: goto op_ins_error;
                        case STR: goto op_ins_error;
                    }
                    break;
                case FLOAT:  goto op_ins_error;
                case STR: goto op_ins_error;
            }
            break;
        case 14: // |
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, *(const int*)u[0] | *(const int*)u[1], ttype); break;
                        case FLOAT: goto op_ins_error;
                        case STR: goto op_ins_error;
                    }
                    break;
                case FLOAT:  goto op_ins_error;
                case STR: goto op_ins_error;
            }
            break;
        case 15: // &&
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, (*(const int*)u[0] != 0) && (*(const int*)u[1] != 0), ttype); break;
                        case FLOAT: setVar(*target, (*(const int*)u[0] != 0) && (*(const float*)u[1] != 0.f), ttype); break;
                        case STR: setVar(*target, (*(const int*)u[0] != 0) && (!((const std::string*)u[1])->empty()), ttype); break;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, (*(const float*)u[0] != 0) && (*(const int*)u[1] != 0), ttype); break;
                        case FLOAT: setVar(*target, (*(const float*)u[0] != 0) && (*(const float*)u[1] != 0.f), ttype); break;
                        case STR: setVar(*target, (*(const float*)u[0] != 0) && (!((const std::string*)u[1])->empty()), ttype); break;
                    }
                    break;
                case STR:
                    switch(t[1])
                    {
                        case INT: setVar(*target, (!((const std::string*)u[0])->empty()) && (*(const int*)u[1] != 0), ttype); break;
                        case FLOAT: setVar(*target, (!((const std::string*)u[0])->empty()) && (*(const float*)u[1] != 0.f), ttype); break;
                        case STR: setVar(*target, (!((const std::string*)u[0])->empty()) && (!((const std::string*)u[1])->empty()), ttype); break;
                    }
                    break;
            }
            break;
        case 16: // ^^
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, (*(const int*)u[0] != 0) != (*(const int*)u[1] != 0), ttype); break;
                        case FLOAT: setVar(*target, (*(const int*)u[0] != 0) != (*(const float*)u[1] != 0.f), ttype); break;
                        case STR: setVar(*target, (*(const int*)u[0] != 0) != (!((const std::string*)u[1])->empty()), ttype); break;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, (*(const float*)u[0] != 0) != (*(const int*)u[1] != 0), ttype); break;
                        case FLOAT: setVar(*target, (*(const float*)u[0] != 0) != (*(const float*)u[1] != 0.f), ttype); break;
                        case STR: setVar(*target, (*(const float*)u[0] != 0) != (!((const std::string*)u[1])->empty()), ttype); break;
                    }
                    break;
                case STR:
                    switch(t[1])
                    {
                        case INT: setVar(*target, (!((const std::string*)u[0])->empty()) != (*(const int*)u[1] != 0), ttype); break;
                        case FLOAT: setVar(*target, (!((const std::string*)u[0])->empty()) != (*(const float*)u[1] != 0.f), ttype); break;
                        case STR: setVar(*target, (!((const std::string*)u[0])->empty()) != (!((const std::string*)u[1])->empty()), ttype); break;
                    }
                    break;
            }
            break;
        case 17: // ||
            switch(t[0])
            {
                case INT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, (*(const int*)u[0] != 0) || (*(const int*)u[1] != 0), ttype); break;
                        case FLOAT: setVar(*target, (*(const int*)u[0] != 0) || (*(const float*)u[1] != 0.f), ttype); break;
                        case STR: setVar(*target, (*(const int*)u[0] != 0) || (!((const std::string*)u[1])->empty()), ttype); break;
                    }
                    break;
                case FLOAT:
                    switch(t[1])
                    {
                        case INT: setVar(*target, (*(const float*)u[0] != 0) || (*(const int*)u[1] != 0), ttype); break;
                        case FLOAT: setVar(*target, (*(const float*)u[0] != 0) || (*(const float*)u[1] != 0.f), ttype); break;
                        case STR: setVar(*target, (*(const float*)u[0] != 0) || (!((const std::string*)u[1])->empty()), ttype); break;
                    }
                    break;
                case STR:
                    switch(t[1])
                    {
                        case INT: setVar(*target, (!((const std::string*)u[0])->empty()) || (*(const int*)u[1] != 0), ttype); break;
                        case FLOAT: setVar(*target, (!((const std::string*)u[0])->empty()) || (*(const float*)u[1] != 0.f), ttype); break;
                        case STR: setVar(*target, (!((const std::string*)u[0])->empty()) || (!((const std::string*)u[1])->empty()), ttype); break;
                    }
                    break;
            }
            break;
        case 18: // ++
            switch(t[0])
            {
                case INT: setVar(*target, *(const int*)u[0]+1, ttype); break;
                case FLOAT: setVar(*target, *(const float*)u[0]+1, ttype); break;
                case STR: goto op_ins_error;
                default: goto op_ins_error;
            }
            break;
        case 19: // --
            switch(t[0])
            {
                case INT: setVar(*target, *(const int*)u[0]-1, ttype); break;
                case FLOAT: setVar(*target, *(const float*)u[0]-1, ttype); break;
                case STR: goto op_ins_error;
                default: goto op_ins_error;
            }
            break;
        default:
            goto op_ins_error;
    }

    return;
op_ins_error:
    setError("malformed operation");
    return;
op_div_error:
    setError("division by zero");
    return;
}

int Script::get_while_loop_point()
{
    #warning "review this function"
    if(code[id].while_map.find(pc) != code[id].while_map.end())
        return code[id].while_map[pc];

    std::vector<const Value*> vs;
    vs.push_back(&code[id].line[pc].params.back());

    if(vs.back()->getType() != RESULT)
    {
        code[id].while_map[pc] = pc;
        return pc;
    }

    int i = pc-1;
    bool found;
    for(; i > 0; --i)
    {
        Line& l = code[id].line[i];

        found = false;
        if(l.op.getType() == COP)
        {
            switch(*l.op.get<int>())
            {
                case 0:
                    for(size_t x = 0; x < vs.size(); ++x)
                    {
                        if(l.params[0] == *vs[x])
                        {
                            vs.erase(vs.begin()+x);
                            found = true;
                            break;
                        }
                    }
                    if(found && (/*l.params[1].getType() == CVAR ||*/ l.params[1].getType() == RESULT))
                        vs.push_back(&l.params[1]);
                    break;
                case 2:
                    if(l.hasResult && l.params.size() == 2)
                    {
                        for(size_t x = 0; x < vs.size(); ++x)
                        {
                            if(l.params[1] == *vs[x])
                            {
                                vs.erase(vs.begin()+x);
                                found = true;
                                break;
                            }
                        }
                        if(found && (/*l.params[0].getType() == CVAR ||*/ l.params[0].getType() == RESULT))
                            vs.push_back(&l.params[0]);
                        break;
                    }
                default:
                {
                    if(l.hasResult)
                    {
                        for(size_t x = 0; x < vs.size(); ++x)
                        {
                            if(l.params.back() == *vs[x])
                            {
                                vs.erase(vs.begin()+x);
                                found = true;
                                break;
                            }
                        }
                    }

                    if(found)
                    {
                        size_t n = l.params.size() - (l.hasResult ? 1 : 0);
                        for(size_t j = 0; j < n; ++j)
                        {
                            if(found && (/*l.params[j].getType() == CVAR ||*/ l.params[j].getType() == RESULT))
                                vs.push_back(&l.params[j]);
                        }
                    }
                }
            }
        }
        else
        {
            if(l.hasResult)
            {
                for(size_t x = 0; x < vs.size(); ++x)
                {
                    if(l.params.back() == *vs[x])
                    {
                        vs.erase(vs.begin()+x);
                        found = true;
                        break;
                    }
                }
            }

            if(found)
            {
                size_t n = l.params.size() - (l.hasResult ? 1 : 0);
                for(size_t j = 0; j < n; ++j)
                {
                    if(found && (/*l.params[j].getType() == CVAR ||*/ l.params[j].getType() == RESULT))
                        vs.push_back(&l.params[j]);
                }
            }
        }
        if(vs.empty())
            break;
    }
    code[id].while_map[pc] = i;
    return i;
}

void Script::addGlobalFunction(const std::string& name, Callback callback, const size_t &argn)
{
    gl_func[name] = argn;
    gl_callback.insert(std::pair<std::string, Callback>(name, callback));
}

void Script::initGlobalVariables(const size_t& n)
{
    globalVars.resize(n);
}

std::vector<Value>& Script::getGlobalVariables()
{
    return globalVars;
}

void Script::clearGlobalVariables()
{
    for(auto &i: globalVars) i.clear();
    globalVars.clear();
}

void Script::printValue(const Value& v, const bool& isContent)
{
    switch(v.getType())
    {
        case INT: std::cout << "value -> " << *v.get<int>() << std::endl; break;
        case FLOAT: std::cout << "value -> " << *v.get<float>() << std::endl; break;
        case STR: std::cout << "value -> " << *v.get<std::string>() << std::endl; break;
        case CVAR: case RESULT: case GVAR:
            if(isContent)
            {
                std::cout << "error" << std::endl;
            }
            else
            {
                std::cout << (v.getType() == RESULT ? "register [" : "variable [") << (v.getType() == GVAR ? "G" : "") << *v.get<int>() << "] -> ";
                if(loaded) printValue(getVar(*v.get<int>(), v.getType()), true);
                else std::cout << "???" << std::endl;
            }
            break;
        default: std::cout << "uninitialized" << std::endl; break;
    }
}

bool Script::rejectReturn(const Line& l)
{
    if(l.hasResult)
    {
        setError("function doesn't return a result");
        return true;
    }
    return false;
}

void Script::funcReturn(const Value& v, Line& l)
{
    if(l.hasResult)
        setVar(*l.params.back().get<int>(), v, l.params.back().getType());
}

void Script::funcReturn(const int& v, Line& l)
{
    if(l.hasResult)
        setVar(*l.params.back().get<int>(), v, l.params.back().getType());
}

void Script::funcReturn(const float& v, Line& l)
{
    if(l.hasResult)
        setVar(*l.params.back().get<int>(), v, l.params.back().getType());
}

void Script::funcReturn(const std::string& v, Line& l)
{
    if(l.hasResult)
        setVar(*l.params.back().get<int>(), v, l.params.back().getType());
}

void Script::_if(Script* s, Line& l)
{
    if(l.params.size() < 1 || l.hasResult)
    {
        s->setError();
        return;
    }
    bool r;
    int type;
    const void* p = s->getValueContent(l.params[0], type);
    switch(type)
    {
        case INT: r = (*(const int*)p) != 0; break;
        case FLOAT: r = (*(const float*)p) != 0.f; break;
        case STR: r = !((const std::string*)p)->empty(); break;
        default: s->setError(); return;
    }

    if(r) s->enterBlock(false);
    else s->skipBlock(true);
}

void Script::_else(Script* s, Line& l)
{
    if(l.hasResult)
    {
        s->setError();
        return;
    }
    s->enterBlock(false);
}

void Script::_return(Script* s, Line& l)
{
    if(l.hasResult)
    {
        s->setError();
        return;
    }
    if(!l.params.empty()) s->ret(&l.params[0]);
    else s->ret(nullptr);
}

void Script::_while(Script* s, Line& l)
{
    if(l.params.size() < 1 || l.hasResult)
    {
        s->setError();
        return;
    }
    bool r;
    int type;
    const void* p = s->getValueContent(l.params[0], type);
    switch(type)
    {
        case INT: r = (*(const int*)p) != 0; break;
        case FLOAT: r = (*(const float*)p) != 0.f; break;
        case STR: r = !((const std::string*)p)->empty(); break;
        default: s->setError(); return;
    }

    if(r) s->enterBlock(true);
    else s->skipBlock(false);
}

void Script::_print(Script* s, Line& l)
{
    if(l.params.size() < 1 || (l.params.size() < 2 && l.hasResult))
    {
        s->setError();
        return;
    }
    int type;
    const void* p = s->getValueContent(l.params[0], type);
    switch(type)
    {
        case INT: std::cout << *(const int*)p << std::endl; break;
        case FLOAT: std::cout << *(const float*)p << std::endl; break;
        case STR: std::cout << *(const std::string*)p << std::endl; break;
        default: s->setError(); return;
    }
}

void Script::_debug(Script* s, Line& l)
{
    if(l.hasResult)
    {
        s->setError();
        return;
    }
}

void Script::_break(Script* s, Line& l)
{
    if(l.hasResult)
    {
        s->setError();
        return;
    }
    s->state = PAUSE;
}
