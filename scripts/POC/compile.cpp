#include "program.h"
#include "ast.h"
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <map>

using std::cout;
using std::cerr;
using std::endl;

struct Translator {
    struct Visit {
        std::string lvalue;
        std::string rvalue;
    };

    int regid = 0;

    std::string alloc_register() {
        char buf[233];
        sprintf(buf, "$%d", regid++);
        return buf;
    }

    std::map<std::string, std::string> regalloc;

    std::string get_register(std::string const &name) {
        auto it = regalloc.find(name);
        if (it == regalloc.end()) {
            auto reg = alloc_register();
            regalloc[name] = reg;
            return reg;
        }
        return it->second;
    }

    void emit(std::string const &str) {
        lines += str + "\n";
    }

    std::string lvalue(Visit &vis) {
        if (vis.lvalue.size() == 0) {
            auto reg = alloc_register();
            vis.lvalue = reg;
            emit(vis.rvalue + " " + reg);
        }
        return vis.lvalue;
    }

    void movalue(Visit &src, std::string const &dst) {
        if (src.lvalue.size() == 0) {
            src.lvalue = dst;
            emit(src.rvalue + " " + dst);
        } else {
            emit("mov " + src.lvalue + " " + dst);
        }
    }

    Visit make_visit(std::string const &lvalue, std::string const &rvalue) {
        return {lvalue, rvalue};
    }

    Visit visit(AST *ast) {
        if (ast->token.type == Token::Type::op) {
            if (ast->token.ident == "=") {
                auto src = visit(ast->args[1].get());
                auto dst = visit(ast->args[0].get());
                movalue(src, dst.lvalue);
                return make_visit("", "");
            }
            auto res = ast->token.ident;
            for (auto const &arg: ast->args) {
                auto vis = visit(arg.get());
                res += " " + lvalue(vis);
            }
            return make_visit("", res);
        } else if (ast->token.type == Token::Type::mem) {
            return make_visit("@" + ast->token.ident, "");
        } else if (ast->token.type == Token::Type::reg) {
            return make_visit(get_register(ast->token.ident), "");
        } else if (ast->token.type == Token::Type::imm) {
            return make_visit("#" + ast->token.ident, "");
        }
        return make_visit("", "");
    }

    std::string lines;

    std::string dump() const {
        return lines;
    }
};

static std::vector<std::string> split_str(std::string const &s, char delimiter) {
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream iss(s);
  while (std::getline(iss, token, delimiter))
    tokens.push_back(token);
  return tokens;
}

struct Unwrapper {
    std::map<std::string, std::string> typing;
    std::map<int, std::pair<std::string, std::string>> casting;
    std::stringstream oss;

    Unwrapper() {
        typing["@a"] = "f3";
        typing["@b"] = "f1";
    }

    static std::string opchar_to_name(std::string const &op) {
        if (op == "+") return "add";
        if (op == "-") return "sub";
        if (op == "*") return "mul";
        if (op == "/") return "div";
        if (op == "mov") return "mov";
        return "";
    }

    template <class ...Ts>
    static void error(Ts &&...ts) {
        (cerr << "ERROR: " << ... << ts) << endl;
        exit(-1);
    }

    std::string determine_type(std::string const &exp) const {
        if (exp[0] == '#') {
            return strchr(exp.substr(1).c_str(), '.') ? "f1" : "i1";
        }
        auto it = typing.find(exp);
        if (it == typing.end()) {
            error("cannot determine type of ", exp);
        }
        return it->second;
    }

    std::string tag_dim(std::string const &exp, int d) const {
        if (exp[0] == '#')
            return exp;
        char buf[233];
        sprintf(buf, "%s.%d", exp.c_str(), d);
        return buf;
    }

    static int get_digit(char c) {
        return c <= '9' ? c - '0' : c - 'A';
    }

    static char put_digit(int n) {
        return n <= 9 ? n + '0' : n - 10 + 'A';
    }

    void emit_op(std::string const &opcode, std::string const &dst,
        std::vector<std::string> const &args) {
        auto dsttype = determine_type(dst);
        int dim = get_digit(dsttype[1]);
        for (int d = 0; d < dim; d++) {
            auto opinst = dsttype[0] + opchar_to_name(opcode);
            oss << opinst << " " << tag_dim(dst, d);
            for (auto const &arg: args) {
                auto argdim = get_digit(determine_type(arg)[1]);
                oss << " " << tag_dim(arg, d % argdim);
            }
            oss << '\n';
        }
    }

    std::string promote_type(std::string const &lhs, std::string const &rhs) {
        char stype = lhs[0] <= rhs[0] ? lhs[0] : rhs[0];
        char dim = 0;
        if (lhs[1] == '1') {
            dim = rhs[1];
        } else if (rhs[1] == '1') {
            dim = lhs[1];
        } else {
            if (lhs[1] != rhs[1]) {
                error("vector dimension mismatch: ", lhs[1], " != ", rhs[1]);
            }
            dim = lhs[1];
        }
        return std::string() + stype + dim;
    }

    void op_promote_type(std::string const &dst,
        std::string const &opcode, std::vector<std::string> const &types) {
        auto curtype = types[0];
        for (int i = 1; i < types.size(); i++) {
            auto const &type = types[i];
            curtype = promote_type(curtype, type);
        }
        auto it = typing.find(dst);
        if (it == typing.end()) {
            typing[dst] = curtype;
            oss << "def " << dst << " " << curtype << '\n';
        } else {
            if (it->second != curtype) {
                if (dst[0] == '@') {
                    if (promote_type(it->second, curtype) != it->second) {
                        error("cannot cast: ", it->second, " <- ", curtype);
                    }
                }
                oss << "def " << dst << " " << curtype << '\n';
                typing[dst] = promote_type(it->second, curtype);
            }
        }
    }

    void parse(std::string const &lines) {
        for (auto const &line: split_str(lines, '\n')) {
            if (line.size() == 0) return;
            auto ops = split_str(line, ' ');
            auto opcode = ops[0];
            std::vector<std::string> argtypes;
            for (int i = 1; i < ops.size() - 1; i++) {
                auto arg = ops[i];
                auto type = determine_type(arg);
                argtypes.push_back(type);
            }
            auto dst = ops[ops.size() - 1];
            op_promote_type(dst, opcode, argtypes);
        }

        for (auto const &line: split_str(lines, '\n')) {
            if (line.size() == 0) return;
            auto ops = split_str(line, ' ');
            auto opcode = ops[0];
            std::vector<std::string> args;
            for (int i = 1; i < ops.size() - 1; i++) {
                auto arg = ops[i];
                args.push_back(arg);
            }
            auto dst = ops[ops.size() - 1];
            emit_op(opcode, dst, args);
        }
    }

    std::string dump() const {
        return oss.str();
    }
};

int main() {
    auto code = "@a = @a + @b * ((3 + 1) + 1.4)";
    cout << code << endl;
    cout << "===" << endl;

    Parser p(code);
    auto ast = p.parse();
    cout << ast->dump() << endl;
    cout << "===" << endl;

    Translator t;
    t.visit(ast.get());
    ast = nullptr;
    auto ir = t.dump();
    cout << ir;
    cout << "===" << endl;

    Unwrapper u;
    u.parse(ir);
    auto iir = u.dump();
    cout << iir;
    cout << "===" << endl;

    return 0;
}
