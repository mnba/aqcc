#include "cc.h"

Code *value(int value) { return new_value_code(value); }

Code *addrof_label(Code *reg, char *label)
{
    return new_addrof_label_code(reg, label);
}

Code *addrof(Code *reg, int offset) { return new_addrof_code(reg, offset); }

int temp_reg_table;

void init_temp_reg() { temp_reg_table = 0; }

int get_temp_reg()
{
    for (int i = 0; i < 6; i++) {
        if (temp_reg_table & (1 << i)) continue;
        temp_reg_table |= (1 << i);
        return i + 7;  // corresponding to reg_name's index
    }

    error("no more register");
}

void restore_temp_reg(int i) { temp_reg_table &= ~(1 << (i - 7)); }

const char *reg_name(int byte, int i)
{
    const char *lreg[13];
    lreg[0] = "%al";
    lreg[1] = "%dil";
    lreg[2] = "%sil";
    lreg[3] = "%dl";
    lreg[4] = "%cl";
    lreg[5] = "%r8b";
    lreg[6] = "%r9b";
    lreg[7] = "%r10b";
    lreg[8] = "%r11b";
    lreg[9] = "%r12b";
    lreg[10] = "%r13b";
    lreg[11] = "%r14b";
    lreg[12] = "%r15b";
    const char *xreg[13];
    xreg[0] = "%ax";
    xreg[1] = "%di";
    xreg[2] = "%si";
    xreg[3] = "%dx";
    xreg[4] = "%cx";
    xreg[5] = "%r8w";
    xreg[6] = "%r9w";
    xreg[7] = "%r10w";
    xreg[8] = "%r11w";
    xreg[9] = "%r12w";
    xreg[10] = "%r13w";
    xreg[11] = "%r14w";
    xreg[12] = "%r15w";
    const char *ereg[13];
    ereg[0] = "%eax";
    ereg[1] = "%edi";
    ereg[2] = "%esi";
    ereg[3] = "%edx";
    ereg[4] = "%ecx";
    ereg[5] = "%r8d";
    ereg[6] = "%r9d";
    ereg[7] = "%r10d";
    ereg[8] = "%r11d";
    ereg[9] = "%r12d";
    ereg[10] = "%r13d";
    ereg[11] = "%r14d";
    ereg[12] = "%r15d";
    const char *rreg[13];
    rreg[0] = "%rax";
    rreg[1] = "%rdi";
    rreg[2] = "%rsi";
    rreg[3] = "%rdx";
    rreg[4] = "%rcx";
    rreg[5] = "%r8";
    rreg[6] = "%r9";
    rreg[7] = "%r10";
    rreg[8] = "%r11";
    rreg[9] = "%r12";
    rreg[10] = "%r13";
    rreg[11] = "%r14";
    rreg[12] = "%r15";

    assert(0 <= i && i <= 12);

    switch (byte) {
        case 1:
            return lreg[i];
        case 2:
            return xreg[i];
        case 4:
            return ereg[i];
        case 8:
            return rreg[i];
        default:
            assert(0);
    }
}

int lookup_member_offset(Vector *members, char *member)
{
    // O(n)
    int size = vector_size(members);
    for (int i = 0; i < size; i++) {
        StructMember *sm = (StructMember *)vector_get(members, i);
        if ((sm->type->kind == TY_STRUCT || sm->type->kind == TY_UNION) &&
            sm->name == NULL) {
            // nested anonymous struct with no varname.
            // its members can be accessed like parent struct's members.
            int offset = lookup_member_offset(sm->type->members, member);
            if (offset >= 0) return sm->offset + offset;
        }
        else if (strcmp(sm->name, member) == 0) {
            return sm->offset;
        }
    }
    return -1;
}

typedef struct {
    char *continue_label, *break_label;
    int reg_save_area_stack_idx, overflow_arg_area_stack_idx;
    Vector *code;
} CodeEnv;
CodeEnv *codeenv;

#define SAVE_BREAK_CXT char *break_cxt__org_break_label = codeenv->break_label;
#define RESTORE_BREAK_CXT codeenv->break_label = break_cxt__org_break_label;
#define SAVE_CONTINUE_CXT \
    char *continue_cxt__org_continue_label = codeenv->continue_label;
#define RESTORE_CONTINUE_CXT \
    codeenv->continue_label = continue_cxt__org_continue_label;
#define SAVE_VARIADIC_CXT                                \
    int save_variadic_cxt__reg_save_area_stack_idx =     \
            codeenv->reg_save_area_stack_idx,            \
        save_variadic_cxt__overflow_arg_area_stack_idx = \
            codeenv->overflow_arg_area_stack_idx;

#define RESTORE_VARIADIC_CXT                        \
    codeenv->reg_save_area_stack_idx =              \
        save_variadic_cxt__reg_save_area_stack_idx; \
    codeenv->overflow_arg_area_stack_idx =          \
        save_variadic_cxt__overflow_arg_area_stack_idx;

void init_code_env()
{
    codeenv = (CodeEnv *)safe_malloc(sizeof(CodeEnv));
    codeenv->continue_label = codeenv->break_label = NULL;
    codeenv->reg_save_area_stack_idx = 0;
    codeenv->code = new_vector();
}

void appcode(Code *code) { vector_push_back(codeenv->code, code); }

void appcomment(char *str)
{
    Code *code = new_code(CD_COMMENT);
    code->sval = str;
    appcode(code);
}

Code *last_appended_code()
{
    for (int i = vector_size(codeenv->code) - 1; i >= 0; i--) {
        Code *str = vector_get(codeenv->code, i);
        if (str) return str;
    }

    assert(0);
}

void add_read_dep(Code *dep)
{
    vector_push_back(last_appended_code()->read_dep, dep);
}

void disable_elimination() { last_appended_code()->can_be_eliminated = 0; }

int nbyte_of_reg(int reg)
{
    int ret = (reg & (REG_8 | REG_16 | REG_32 | REG_64)) >> 5;
    assert(ret == 1 || ret == 2 || ret == 3 || ret == 4);
    return ret;
}

Type *x86_64_analyze_type(Type *type)
{
    if (type == NULL) return NULL;

    switch (type->kind) {
        case TY_INT:
            type->nbytes = 4;
            break;

        case TY_CHAR:
            type->nbytes = 1;
            break;

        case TY_PTR:
            type->ptr_of = x86_64_analyze_type(type->ptr_of);
            type->nbytes = 8;
            break;

        case TY_ARY:
            type->ary_of = x86_64_analyze_type(type->ary_of);
            type->nbytes = type->len * type->ary_of->nbytes;
            break;

        case TY_STRUCT: {
            if (type->members == NULL) break;

            int offset = 0;
            for (int i = 0; i < vector_size(type->members); i++) {
                StructMember *member =
                    (StructMember *)vector_get(type->members, i);
                member->type = x86_64_analyze_type(member->type);

                // calc offset
                offset = roundup(offset, alignment_of(member->type));
                member->offset = offset;
                offset += member->type->nbytes;
            }
            type->nbytes = roundup(offset, alignment_of(type));
        } break;

        case TY_UNION: {
            if (type->members == NULL) break;

            int max_nbytes = 0;
            for (int i = 0; i < vector_size(type->members); i++) {
                StructMember *member =
                    (StructMember *)vector_get(type->members, i);
                member->type = x86_64_analyze_type(member->type);

                // offset is always zero.
                member->offset = 0;
                max_nbytes = max(max_nbytes, member->type->nbytes);
            }
            type->nbytes = roundup(max_nbytes, alignment_of(type));
        } break;

        case TY_ENUM:
            break;

        case TY_VOID:
            break;

        default:
            assert(0);
    }

    return type;
}

int x86_64_eval_ast_int(AST *ast)
{
    assert(ast != NULL);

    switch (ast->kind) {
        case AST_INT:
            return ast->ival;

        case AST_SIZEOF:
            return ast->lhs->type->nbytes;

        case AST_ADD:
        case AST_SUB:
        case AST_MUL:
        case AST_DIV:
        case AST_REM:
        case AST_LSHIFT:
        case AST_RSHIFT:
        case AST_LT:
        case AST_LTE:
        case AST_EQ:
        case AST_AND:
        case AST_XOR:
        case AST_OR:
        case AST_LAND:
        case AST_LOR:
        case AST_NEQ: {
            int lhs = x86_64_eval_ast_int(ast->lhs),
                rhs = x86_64_eval_ast_int(ast->rhs), ret = 0;

            switch (ast->kind) {
                case AST_ADD:
                    ret = lhs + rhs;
                    break;
                case AST_SUB:
                    ret = lhs - rhs;
                    break;
                case AST_MUL:
                    ret = lhs * rhs;
                    break;
                case AST_DIV:
                    ret = lhs / rhs;
                    break;
                case AST_REM:
                    ret = lhs % rhs;
                    break;
                case AST_LSHIFT:
                    ret = lhs << rhs;
                    break;
                case AST_RSHIFT:
                    ret = lhs >> rhs;
                    break;
                case AST_LT:
                    ret = lhs < rhs;
                    break;
                case AST_LTE:
                    ret = lhs <= rhs;
                    break;
                case AST_EQ:
                    ret = lhs == rhs;
                    break;
                case AST_AND:
                    ret = lhs & rhs;
                    break;
                case AST_XOR:
                    ret = lhs ^ rhs;
                    break;
                case AST_OR:
                    ret = lhs | rhs;
                    break;
                case AST_LAND:
                    ret = lhs && rhs;
                    break;
                case AST_LOR:
                    ret = lhs || rhs;
                    break;
                case AST_NEQ:
                    ret = lhs != rhs;
                    break;
                default:
                    assert(0);
            }
            return ret;
        }

        case AST_COMPL:
        case AST_UNARY_MINUS:
        case AST_NOT:
        case AST_CAST:
        case AST_CONSTANT: {
            int lhs = x86_64_eval_ast_int(ast->lhs), ret = 0;
            switch (ast->kind) {
                case AST_COMPL:
                    ret = ~lhs;
                    break;
                case AST_UNARY_MINUS:
                    ret = -lhs;
                    break;
                case AST_NOT:
                    ret = !lhs;
                    break;
                case AST_CAST:
                case AST_CONSTANT:
                    ret = lhs;
                    break;
                default:
                    assert(0);
            }
            return ret;
        }

        case AST_COND: {
            int cond = x86_64_eval_ast_int(ast->cond),
                then = x86_64_eval_ast_int(ast->then),
                els = x86_64_eval_ast_int(ast->els);
            return cond ? then : els;
        }

        default:
            assert(0);
    }
}

// all Type* should pass x86_64_analyze_type().
// all AST* should pass x86_64_analyze_ast_detail()
AST *x86_64_analyze_ast_detail(AST *ast)
{
    if (ast == NULL) return NULL;

    switch (ast->kind) {
        case AST_INT:
        case AST_LVAR:
        case AST_GVAR:
            ast->type = x86_64_analyze_type(ast->type);
            break;

        case AST_ADD:
        case AST_SUB:
        case AST_MUL:
        case AST_DIV:
        case AST_REM:
        case AST_LSHIFT:
        case AST_RSHIFT:
        case AST_LT:
        case AST_LTE:
        case AST_EQ:
        case AST_AND:
        case AST_XOR:
        case AST_OR:
        case AST_LAND:
        case AST_LOR:
        case AST_ASSIGN:
        case AST_VA_START:
            ast->type = x86_64_analyze_type(ast->type);
            ast->lhs = x86_64_analyze_ast_detail(ast->lhs);
            ast->rhs = x86_64_analyze_ast_detail(ast->rhs);
            break;

        case AST_COMPL:
        case AST_UNARY_MINUS:
        case AST_PREINC:
        case AST_POSTINC:
        case AST_PREDEC:
        case AST_POSTDEC:
        case AST_ADDR:
        case AST_INDIR:
        case AST_CAST:
        case AST_CHAR2INT:
        case AST_LVALUE2RVALUE:
        case AST_VA_ARG_INT:
        case AST_VA_ARG_CHARP:
            ast->type = x86_64_analyze_type(ast->type);
            ast->lhs = x86_64_analyze_ast_detail(ast->lhs);
            break;

        case AST_ARY2PTR:
            ast->type = x86_64_analyze_type(ast->type);
            ast->ary = x86_64_analyze_ast_detail(ast->ary);
            break;

        case AST_EXPR_LIST:
            ast->type = x86_64_analyze_type(ast->type);

            for (int i = 0; i < vector_size(ast->exprs); i++) {
                AST *expr = (AST *)vector_get(ast->exprs, i);
                vector_set(ast->exprs, i, x86_64_analyze_ast_detail(expr));
            }
            break;

        case AST_COND:
            ast->type = x86_64_analyze_type(ast->type);
            ast->cond = x86_64_analyze_ast_detail(ast->cond);
            ast->then = x86_64_analyze_ast_detail(ast->then);
            ast->els = x86_64_analyze_ast_detail(ast->els);
            break;

        case AST_FUNCCALL:
            ast->type = x86_64_analyze_type(ast->type);
            for (int i = 0; i < vector_size(ast->args); i++) {
                AST *arg = (AST *)vector_get(ast->args, i);
                vector_set(ast->args, i, x86_64_analyze_ast_detail(arg));
            }
            break;

        case AST_IF:
            ast->cond = x86_64_analyze_ast_detail(ast->cond);
            ast->then = x86_64_analyze_ast_detail(ast->then);
            ast->els = x86_64_analyze_ast_detail(ast->els);
            break;

        case AST_SWITCH:
            ast->target = x86_64_analyze_ast_detail(ast->target);
            ast->switch_body = x86_64_analyze_ast_detail(ast->switch_body);
            for (int i = 0; i < vector_size(ast->cases); i++) {
                SwitchCase *cas = (SwitchCase *)vector_get(ast->cases, i);
                cas->cond = x86_64_analyze_ast_detail(cas->cond);
                vector_set(ast->cases, i, cas);
            }
            break;

        case AST_FOR:
            ast->initer = x86_64_analyze_ast_detail(ast->initer);
            ast->midcond = x86_64_analyze_ast_detail(ast->midcond);
            ast->iterer = x86_64_analyze_ast_detail(ast->iterer);
            ast->for_body = x86_64_analyze_ast_detail(ast->for_body);
            break;

        case AST_DOWHILE:
            ast->cond = x86_64_analyze_ast_detail(ast->cond);
            ast->then = x86_64_analyze_ast_detail(ast->then);
            break;

        case AST_RETURN:
        case AST_EXPR_STMT:
            ast->lhs = x86_64_analyze_ast_detail(ast->lhs);
            break;

        case AST_MEMBER_REF:
            ast->type = x86_64_analyze_type(ast->type);
            ast->stsrc = x86_64_analyze_ast_detail(ast->stsrc);
            break;

        case AST_COMPOUND:
            for (int i = 0; i < vector_size(ast->stmts); i++) {
                AST *stmt = (AST *)vector_get(ast->stmts, i);
                vector_set(ast->stmts, i, x86_64_analyze_ast_detail(stmt));
            }
            break;

        case AST_LVAR_DECL_INIT:
        case AST_GVAR_DECL_INIT:
            ast->lhs = x86_64_analyze_ast_detail(ast->lhs);
            ast->rhs = x86_64_analyze_ast_detail(ast->rhs);
            break;

        case AST_DECL_LIST:
            for (int i = 0; i < vector_size(ast->decls); i++) {
                AST *decl = (AST *)vector_get(ast->decls, i);
                vector_set(ast->decls, i, x86_64_analyze_ast_detail(decl));
            }
            break;

        case AST_FUNCDEF:
            ast->type = x86_64_analyze_type(ast->type);
            if (ast->params) {
                for (int i = 0; i < vector_size(ast->params); i++) {
                    AST *param = (AST *)vector_get(ast->params, i);
                    param->type = x86_64_analyze_type(param->type);
                }
            }
            ast->body = x86_64_analyze_ast_detail(ast->body);
            break;

        case AST_NOP:
        case AST_GVAR_DECL:
        case AST_LVAR_DECL:
        case AST_FUNC_DECL:
        case AST_GOTO:
        case AST_BREAK:
        case AST_CONTINUE:
            break;

        case AST_LABEL:
            ast->label_stmt = x86_64_analyze_ast_detail(ast->label_stmt);
            break;

        case AST_SIZEOF:
            if (ast->lhs->kind == AST_NOP)
                ast->lhs->type = x86_64_analyze_type(ast->lhs->type);
            else
                ast->lhs = x86_64_analyze_ast_detail(ast->lhs);
            ast->type = x86_64_analyze_type(ast->type);
            break;

        case AST_CONSTANT:
            assert(ast->type->kind == TY_INT);
            ast->lhs = x86_64_analyze_ast_detail(ast->lhs);
            ast = new_int_ast(x86_64_eval_ast_int(ast));
            break;

        default:
            assert(0);
    }

    return ast;
}

void x86_64_analyze_ast(Vector *asts)
{
    int nasts = vector_size(asts);
    for (int i = 0; i < nasts; i++) {
        AST *ast = (AST *)vector_get(asts, i);
        vector_set(asts, i, x86_64_analyze_ast_detail(ast));
    }
}

void generate_basic_block_start_maker()
{
    appcomment("BLOCK START");
    appcode(new_code(MRK_BASIC_BLOCK_START));
}

void generate_basic_block_end_marker()
{
    appcode(new_code(MRK_BASIC_BLOCK_END));
    appcomment("BLOCK END");
}

void generate_funcdef_start_marker()
{
    appcomment("FUNCDEF START");
    appcode(new_code(MRK_FUNCDEF_START));
}

void generate_funcdef_end_marker()
{
    appcode(new_code(MRK_FUNCDEF_END));
    appcomment("FUNCDEF END");
}

void generate_funcdef_return_marker() { appcode(new_code(MRK_FUNCDEF_RETURN)); }

void generate_mov_mem_reg(int nbyte, int src_reg, int dst_reg)
{
    switch (nbyte) {
        case 1: {
            appcode(MOVSBL(addrof(nbyte_reg(8, src_reg), 0),
                           new_code(reg_of_nbyte(4, dst_reg))));
        } break;
        case 4:
        case 8:
            appcode(MOV(addrof(nbyte_reg(8, src_reg), 0),
                        new_code(reg_of_nbyte(nbyte, dst_reg))));
            break;
        default:
            assert(0);
    }
}

int x86_64_generate_code_detail(AST *ast)
{
    switch (ast->kind) {
        case AST_INT: {
            int reg = get_temp_reg();
            appcode(MOV(value(ast->ival), nbyte_reg(4, reg)));
            return reg;
        }

        case AST_RETURN: {
            assert(temp_reg_table == 0);
            if (ast->lhs) {
                int reg = x86_64_generate_code_detail(ast->lhs);
                restore_temp_reg(reg);
                appcode(MOV(nbyte_reg(8, reg), RAX()));
            }
            else {
                appcode(MOV(value(0), RAX()));
            }
            assert(temp_reg_table == 0);
            generate_funcdef_return_marker();
            appcode(MOV(RBP(), RSP()));
            appcode(POP(RBP()));
            appcode(RET());
            return -1;
        }

        case AST_ADD: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);

            // int + ptr
            // TODO: shift
            // TODO: long
            if (match_type2(ast->lhs, ast->rhs, TY_INT, TY_PTR)) {
                appcode(MOVSLQ(nbyte_reg(4, lreg), nbyte_reg(8, lreg)));
                appcode(CLTQ());
                appcode(IMUL(value(ast->rhs->type->ptr_of->nbytes),
                             nbyte_reg(8, lreg)));
            }

            int nbytes = ast->type->nbytes;
            appcode(ADD(nbyte_reg(nbytes, lreg), nbyte_reg(nbytes, rreg)));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_SUB: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);

            // ptr - int
            // TODO: shift
            // TODO: long
            if (match_type2(ast->lhs, ast->rhs, TY_PTR, TY_INT)) {
                appcode(MOVSLQ(nbyte_reg(4, rreg), nbyte_reg(8, rreg)));
                appcode(CLTQ());
                appcode(IMUL(value(ast->lhs->type->ptr_of->nbytes),
                             nbyte_reg(8, rreg)));
            }

            // ptr - ptr
            if (match_type2(ast->lhs, ast->rhs, TY_PTR, TY_PTR)) {
                // assume pointer size is 8.
                appcode(SUB(nbyte_reg(8, rreg), nbyte_reg(8, lreg)));
                switch (ast->lhs->type->ptr_of->nbytes) {
                    case 1:
                        break;
                    case 4:
                        appcode(SAR(value(2), nbyte_reg(8, lreg)));
                        break;
                    case 8:
                        appcode(SAR(value(3), nbyte_reg(8, lreg)));
                        break;
                    default:
                        assert(0);  // TODO: idiv?
                }
            }
            else {
                int nbytes = ast->type->nbytes;
                appcode(SUB(nbyte_reg(nbytes, rreg), nbyte_reg(nbytes, lreg)));
            }

            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_MUL: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);
            int nbytes = 4;  // TODO: long
            appcode(IMUL(nbyte_reg(nbytes, lreg), nbyte_reg(nbytes, rreg)));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_DIV: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);
            appcode(MOV(nbyte_reg(ast->type->nbytes, lreg),
                        nbyte_reg(ast->type->nbytes, 0)));
            appcode(CLTD());  // TODO: assume EAX
            appcode(IDIV(nbyte_reg(ast->type->nbytes, rreg)));
            add_read_dep(RAX());
            appcode(MOV(RAX(), nbyte_reg(8, rreg)));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_REM: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);
            appcode(MOV(nbyte_reg(ast->type->nbytes, lreg),
                        nbyte_reg(ast->type->nbytes, 0)));
            appcode(CLTD());  // TODO: assume EAX
            appcode(IDIV(nbyte_reg(ast->type->nbytes, rreg)));
            add_read_dep(RAX());
            appcode(MOV(RDX(), nbyte_reg(8, rreg)));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_UNARY_MINUS: {
            int reg = x86_64_generate_code_detail(ast->lhs);
            appcode(NEG(nbyte_reg(ast->type->nbytes, reg)));
            return reg;
        }

        case AST_COMPL: {
            int reg = x86_64_generate_code_detail(ast->lhs);
            appcode(NOT(nbyte_reg(ast->type->nbytes, reg)));
            return reg;
        }

        case AST_LSHIFT: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);
            appcode(MOV(nbyte_reg(1, rreg), CL()));
            appcode(SAL(CL(), nbyte_reg(ast->type->nbytes, lreg)));
            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_RSHIFT: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);
            appcode(MOV(nbyte_reg(1, rreg), CL()));
            appcode(SAR(CL(), nbyte_reg(ast->type->nbytes, lreg)));
            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_LT: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);
            appcode(CMP(nbyte_reg(ast->type->nbytes, rreg),
                        nbyte_reg(ast->type->nbytes, lreg)));
            appcode(SETL(AL()));
            appcode(MOVZB(AL(), nbyte_reg(ast->type->nbytes, lreg)));
            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_LTE: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);
            appcode(CMP(nbyte_reg(ast->type->nbytes, rreg),
                        nbyte_reg(ast->type->nbytes, lreg)));
            appcode(SETLE(AL()));
            appcode(MOVZB(AL(), nbyte_reg(ast->type->nbytes, lreg)));
            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_EQ: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);
            appcode(CMP(nbyte_reg(ast->type->nbytes, rreg),
                        nbyte_reg(ast->type->nbytes, lreg)));
            appcode(SETE(AL()));
            appcode(MOVZB(AL(), nbyte_reg(ast->type->nbytes, lreg)));
            restore_temp_reg(rreg);
            return lreg;
        }

        case AST_AND: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);
            appcode(AND(nbyte_reg(ast->type->nbytes, lreg),
                        nbyte_reg(ast->type->nbytes, rreg)));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_XOR: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);
            appcode(XOR(nbyte_reg(ast->type->nbytes, lreg),
                        nbyte_reg(ast->type->nbytes, rreg)));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_OR: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);
            appcode(OR(nbyte_reg(ast->type->nbytes, lreg),
                       nbyte_reg(ast->type->nbytes, rreg)));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_LAND: {
            char *false_label = make_label_string(),
                 *exit_label = make_label_string(),
                 *beyond_label0 = make_label_string(),
                 *beyond_label1 = make_label_string();
            int lreg = x86_64_generate_code_detail(ast->lhs);
            restore_temp_reg(lreg);
            appcode(CMP(value(0), nbyte_reg(ast->type->nbytes, lreg)));

            // JE(false_label)
            appcode(JNE(beyond_label0));
            appcode(JMP(false_label));
            appcode(LABEL(beyond_label0));

            // don't execute rhs expression if lhs is false.
            int rreg = x86_64_generate_code_detail(ast->rhs);
            Code *rreg_code = nbyte_reg(ast->type->nbytes, rreg);
            appcode(CMP(value(0), rreg_code));

            // JE(false_label)
            appcode(JNE(beyond_label1));
            appcode(JMP(false_label));
            appcode(LABEL(beyond_label1));

            appcode(MOV(value(1), rreg_code));
            appcode(JMP(exit_label));
            appcode(LABEL(false_label));
            appcode(MOV(value(0), rreg_code));
            appcode(LABEL(exit_label));
            return rreg;
        }

        case AST_LOR: {
            char *true_label = make_label_string(),
                 *exit_label = make_label_string(),
                 *beyond_label0 = make_label_string(),
                 *beyond_label1 = make_label_string();
            int lreg = x86_64_generate_code_detail(ast->lhs);
            restore_temp_reg(lreg);
            appcode(CMP(value(0), nbyte_reg(ast->type->nbytes, lreg)));

            // JNE(true_label);
            appcode(JE(beyond_label0));
            appcode(JMP(true_label));
            appcode(LABEL(beyond_label0));

            // don't execute rhs expression if lhs is true.
            int rreg = x86_64_generate_code_detail(ast->rhs);
            Code *rreg_code = nbyte_reg(ast->type->nbytes, rreg);
            appcode(CMP(value(0), rreg_code));

            // JNE(true_label);
            appcode(JE(beyond_label1));
            appcode(JMP(true_label));
            appcode(LABEL(beyond_label1));

            appcode(CMP(value(0), rreg_code));
            appcode(JMP(exit_label));
            appcode(LABEL(true_label));
            appcode(MOV(value(1), rreg_code));
            appcode(LABEL(exit_label));
            return rreg;
        }

        case AST_FUNCDEF: {
            // allocate stack
            int stack_idx = 0;
            for (int i = ast->params ? max(0, vector_size(ast->params) - 6) : 0;
                 i < vector_size(ast->env->scoped_vars); i++) {
                AST *var = (AST *)(vector_get(ast->env->scoped_vars, i));

                stack_idx -= var->type->nbytes;
                var->stack_idx = stack_idx;
            }

            stack_idx -= (!!ast->is_variadic) * 48;

            // generate code
            appcode(GLOBAL(ast->fname));
            appcode(LABEL(ast->fname));
            appcode(PUSH(RBP()));
            appcode(MOV(RSP(), RBP()));
            int needed_stack_size = roundup(-stack_idx, 16);
            if (needed_stack_size > 0)
                appcode(SUB(value(needed_stack_size), RSP()));
            generate_funcdef_start_marker();

            // assign param to localvar
            if (ast->params) {
                for (int i = 0; i < vector_size(ast->params); i++) {
                    AST *var = lookup_var(
                        ast->env, ((AST *)vector_get(ast->params, i))->varname);
                    if (i < 6)
                        appcode(MOV(nbyte_reg(var->type->nbytes, i + 1),
                                    addrof(RBP(), var->stack_idx)));
                    else
                        // should avoid return pointer and saved %rbp
                        var->stack_idx = 16 + (i - 6) * 8;
                }
            }

            // place Register Save Area if the function has variadic params.
            if (ast->is_variadic)
                for (int i = 0; i < 6; i++)
                    appcode(MOV(nbyte_reg(8, i + 1),
                                addrof(RBP(), stack_idx + i * 8)));

            // generate body
            SAVE_VARIADIC_CXT;
            codeenv->reg_save_area_stack_idx = stack_idx;
            codeenv->overflow_arg_area_stack_idx =
                ast->params ? max(0, vector_size(ast->params) - 6) * 8 + 16
                            : -1;
            assert(temp_reg_table == 0);
            x86_64_generate_code_detail(ast->body);
            assert(temp_reg_table == 0);
            RESTORE_VARIADIC_CXT;

            if (ast->type->kind != TY_VOID) appcode(MOV(value(0), RAX()));
            generate_funcdef_end_marker();
            appcode(MOV(RBP(), RSP()));
            appcode(POP(RBP()));
            appcode(RET());
            return -1;
        }

        case AST_ASSIGN: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = x86_64_generate_code_detail(ast->rhs);

            appcode(MOV(nbyte_reg(ast->type->nbytes, rreg),
                        addrof(nbyte_reg(8, lreg), 0)));
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_EXPR_LIST: {
            int reg;
            for (int i = 0; i < vector_size(ast->exprs); i++) {
                reg = x86_64_generate_code_detail(
                    (AST *)vector_get(ast->exprs, i));
                if (i != vector_size(ast->exprs) - 1) restore_temp_reg(reg);
            }
            return reg;
        }

        case AST_LVAR: {
            int reg = get_temp_reg();
            Code *reg_code = nbyte_reg(8, reg);
            if (ast->type->is_static || ast->type->is_extern)
                appcode(LEA(addrof_label(RIP(), ast->gen_varname), reg_code));
            else
                appcode(LEA(addrof(RBP(), ast->stack_idx), reg_code));
            return reg;
        }

        case AST_GVAR: {
            int reg = get_temp_reg();
            Code *reg_code = nbyte_reg(8, reg);
            appcode(LEA(addrof_label(RIP(), ast->gen_varname), reg_code));
            return reg;
        }

        case AST_FUNCCALL: {
            appcode(PUSH(R10()));
            appcode(PUSH(R11()));
            for (int i = vector_size(ast->args) - 1; i >= 0; i--) {
                int reg = x86_64_generate_code_detail(
                    (AST *)(vector_get(ast->args, i)));
                appcode(PUSH(nbyte_reg(8, reg)));
                restore_temp_reg(reg);
            }
            for (int i = 0; i < min(6, vector_size(ast->args)); i++)
                appcode(POP(nbyte_reg(8, i + 1)));
            appcode(MOV(value(0), EAX()));
            disable_elimination();

            {
                Code *code = new_code(INST_CALL);
                code->label = ast->fname;
                appcode(code);
            }
            appcode(ADD(value(8 * max(0, vector_size(ast->args) - 6)), RSP()));
            appcode(POP(R11()));
            appcode(POP(R10()));
            int reg = get_temp_reg();
            appcode(MOV(RAX(), nbyte_reg(8, reg)));
            return reg;
        }

        case AST_POSTINC: {
            int lreg = x86_64_generate_code_detail(ast->lhs);
            Code *lreg_code = nbyte_reg(8, lreg);
            int reg = get_temp_reg();
            generate_mov_mem_reg(ast->type->nbytes, lreg, reg);

            if (match_type(ast->lhs, TY_PTR)) {
                appcode(ADDQ(value(ast->lhs->type->ptr_of->nbytes),
                             addrof(lreg_code, 0)));
            }
            else {
                switch (ast->lhs->type->nbytes) {
                    case 4:
                        appcode(INCL(addrof(lreg_code, 0)));
                        break;
                    case 8:
                        appcode(INCQ(addrof(lreg_code, 0)));
                        break;
                    default:
                        assert(0);
                }
            }

            restore_temp_reg(lreg);
            return reg;
        }

        case AST_PREINC: {
            int lreg = x86_64_generate_code_detail(ast->lhs);
            Code *lreg_code = nbyte_reg(8, lreg);
            int reg = get_temp_reg();

            if (match_type(ast->lhs, TY_PTR)) {
                appcode(ADDQ(value(ast->lhs->type->ptr_of->nbytes),
                             addrof(lreg_code, 0)));
            }
            else {
                switch (ast->lhs->type->nbytes) {
                    case 4:
                        appcode(INCL(addrof(lreg_code, 0)));
                        break;
                    case 8:
                        appcode(INCQ(addrof(lreg_code, 0)));
                        break;
                    default:
                        assert(0);
                }
            }

            generate_mov_mem_reg(ast->type->nbytes, lreg, reg);

            restore_temp_reg(lreg);
            return reg;
        }

        case AST_POSTDEC: {
            int lreg = x86_64_generate_code_detail(ast->lhs);
            Code *lreg_code = nbyte_reg(8, lreg);
            int reg = get_temp_reg();
            generate_mov_mem_reg(ast->type->nbytes, lreg, reg);

            if (match_type(ast->lhs, TY_PTR)) {
                appcode(ADDQ(value(-ast->lhs->type->ptr_of->nbytes),
                             addrof(lreg_code, 0)));
            }
            else {
                switch (ast->lhs->type->nbytes) {
                    case 4:
                        appcode(DECL(addrof(lreg_code, 0)));
                        break;
                    case 8:
                        appcode(DECQ(addrof(lreg_code, 0)));
                        break;
                    default:
                        assert(0);
                }
            }

            restore_temp_reg(lreg);
            return reg;
        }

        case AST_PREDEC: {
            int lreg = x86_64_generate_code_detail(ast->lhs);
            Code *lreg_code = nbyte_reg(8, lreg);
            int reg = get_temp_reg();

            if (match_type(ast->lhs, TY_PTR)) {
                appcode(ADDQ(value(-ast->lhs->type->ptr_of->nbytes),
                             addrof(lreg_code, 0)));
            }
            else {
                switch (ast->lhs->type->nbytes) {
                    case 4:
                        appcode(DECL(addrof(lreg_code, 0)));
                        break;
                    case 8:
                        appcode(DECQ(addrof(lreg_code, 0)));
                        break;
                    default:
                        assert(0);
                }
            }

            generate_mov_mem_reg(ast->type->nbytes, lreg, reg);

            restore_temp_reg(lreg);
            return reg;
        }

        case AST_ADDR:
        case AST_INDIR:
        case AST_CAST:
            return x86_64_generate_code_detail(ast->lhs);

        case AST_COND: {
            char *false_label = make_label_string(),
                 *exit_label = make_label_string(),
                 *beyond_label = make_label_string();

            int cond_reg = x86_64_generate_code_detail(ast->cond);
            restore_temp_reg(cond_reg);
            appcode(MOV(nbyte_reg(8, cond_reg), RAX()));
            appcode(CMP(value(0), EAX()));

            // JE(false_label);
            appcode(JNE(beyond_label));
            appcode(JMP(false_label));
            appcode(LABEL(beyond_label));

            int then_reg = x86_64_generate_code_detail(ast->then);
            restore_temp_reg(then_reg);
            appcode(PUSH(nbyte_reg(8, then_reg)));
            appcode(JMP(exit_label));
            appcode(LABEL(false_label));
            if (ast->els != NULL) {
                int els_reg = x86_64_generate_code_detail(ast->els);
                restore_temp_reg(els_reg);
                appcode(PUSH(nbyte_reg(8, els_reg)));
            }
            appcode(LABEL(exit_label));
            int reg = get_temp_reg();
            appcode(POP(nbyte_reg(8, reg)));
            return reg;
        }

        case AST_IF: {
            char *false_label = make_label_string(),
                 *exit_label = make_label_string(),
                 *beyond_label = make_label_string();

            int cond_reg = x86_64_generate_code_detail(ast->cond);
            restore_temp_reg(cond_reg);
            appcode(MOV(nbyte_reg(8, cond_reg), RAX()));
            appcode(CMP(value(0), EAX()));

            // JE(false_label)
            appcode(JNE(beyond_label));
            appcode(JMP(false_label));
            appcode(LABEL(beyond_label));

            x86_64_generate_code_detail(ast->then);
            appcode(JMP(exit_label));
            appcode(LABEL(false_label));
            if (ast->els != NULL) x86_64_generate_code_detail(ast->els);
            appcode(LABEL(exit_label));
            return -1;
        }

        case AST_SWITCH: {
            int target_reg = x86_64_generate_code_detail(ast->target);
            restore_temp_reg(target_reg);

            for (int i = 0; i < vector_size(ast->cases); i++) {
                SwitchCase *cas = (SwitchCase *)vector_get(ast->cases, i);
                assert(cas->cond->kind == AST_INT);
                appcode(CMP(value(cas->cond->ival),
                            nbyte_reg(ast->target->type->nbytes, target_reg)));
                // case has been already labeled when analyzing.
                // JE(cas->label_name)
                char *beyond_label = make_label_string();
                appcode(JNE(beyond_label));
                appcode(JMP(cas->label_name));
                appcode(LABEL(beyond_label));
            }
            char *exit_label = make_label_string();
            if (ast->default_label)
                appcode(JMP(ast->default_label));
            else
                appcode(JMP(exit_label));

            SAVE_BREAK_CXT;
            codeenv->break_label = exit_label;
            x86_64_generate_code_detail(ast->switch_body);
            appcode(LABEL(exit_label));
            RESTORE_BREAK_CXT;

            return -1;
        }

        case AST_DOWHILE: {
            SAVE_BREAK_CXT;
            SAVE_CONTINUE_CXT;
            codeenv->break_label = make_label_string();
            codeenv->continue_label = make_label_string();
            char *start_label = make_label_string();

            appcode(LABEL(start_label));
            x86_64_generate_code_detail(ast->then);
            appcode(LABEL(codeenv->continue_label));
            int cond_reg = x86_64_generate_code_detail(ast->cond);
            appcode(
                CMP(value(0), nbyte_reg(ast->cond->type->nbytes, cond_reg)));
            // JNE(start_label)
            char *beyond_label = make_label_string();
            appcode(JE(beyond_label));
            appcode(JMP(start_label));
            appcode(LABEL(beyond_label));

            appcode(LABEL(codeenv->break_label));

            restore_temp_reg(cond_reg);
            RESTORE_BREAK_CXT;
            RESTORE_CONTINUE_CXT;

            return -1;
        }

        case AST_FOR: {
            SAVE_BREAK_CXT;
            SAVE_CONTINUE_CXT;
            codeenv->break_label = make_label_string();
            codeenv->continue_label = make_label_string();
            char *start_label = make_label_string();

            if (ast->initer != NULL) {
                int reg = x86_64_generate_code_detail(ast->initer);
                if (reg != -1) restore_temp_reg(reg);  // if expr
            }
            appcode(LABEL(start_label));
            if (ast->midcond != NULL) {
                int reg = x86_64_generate_code_detail(ast->midcond);
                appcode(
                    CMP(value(0), nbyte_reg(ast->midcond->type->nbytes, reg)));
                // JE(codeenv->break_label)
                char *beyond_label = make_label_string();
                appcode(JNE(beyond_label));
                appcode(JMP(codeenv->break_label));
                appcode(LABEL(beyond_label));
                restore_temp_reg(reg);
            }
            x86_64_generate_code_detail(ast->for_body);
            appcode(LABEL(codeenv->continue_label));
            if (ast->iterer != NULL) {
                int reg = x86_64_generate_code_detail(ast->iterer);
                if (reg != -1) restore_temp_reg(reg);  // if nop
            }
            appcode(JMP(start_label));
            appcode(LABEL(codeenv->break_label));

            RESTORE_BREAK_CXT;
            RESTORE_CONTINUE_CXT;

            return -1;
        }

        case AST_LABEL:
            appcode(LABEL(ast->label_name));
            x86_64_generate_code_detail(ast->label_stmt);
            return -1;

        case AST_BREAK:
            appcode(JMP(codeenv->break_label));
            return -1;

        case AST_CONTINUE:
            appcode(JMP(codeenv->continue_label));
            return -1;

        case AST_GOTO:
            appcode(JMP(ast->label_name));
            return -1;

        case AST_MEMBER_REF: {
            int offset =
                lookup_member_offset(ast->stsrc->type->members, ast->member);
            // the member existence is confirmed when analysis.
            assert(offset >= 0);

            int reg = x86_64_generate_code_detail(ast->stsrc);
            appcode(ADD(value(offset), nbyte_reg(8, reg)));
            return reg;
        }

        case AST_COMPOUND:
            for (int i = 0; i < vector_size(ast->stmts); i++)
                x86_64_generate_code_detail((AST *)vector_get(ast->stmts, i));
            return -1;

        case AST_EXPR_STMT:
            assert(temp_reg_table == 0);
            generate_basic_block_start_maker();
            if (ast->lhs != NULL) {
                int reg = x86_64_generate_code_detail(ast->lhs);
                if (reg != -1) restore_temp_reg(reg);
            }
            assert(temp_reg_table == 0);
            generate_basic_block_end_marker();
            return -1;

        case AST_ARY2PTR:
            return x86_64_generate_code_detail(ast->ary);

        case AST_CHAR2INT:
            return x86_64_generate_code_detail(ast->lhs);

        case AST_LVALUE2RVALUE: {
            int lreg = x86_64_generate_code_detail(ast->lhs),
                rreg = get_temp_reg();
            generate_mov_mem_reg(ast->type->nbytes, lreg, rreg);
            restore_temp_reg(lreg);
            return rreg;
        }

        case AST_LVAR_DECL_INIT:
        case AST_GVAR_DECL_INIT: {
            x86_64_generate_code_detail(ast->lhs);
            int rreg = x86_64_generate_code_detail(ast->rhs);
            if (rreg != -1) restore_temp_reg(rreg);
            return -1;
        }

        case AST_DECL_LIST:
            for (int i = 0; i < vector_size(ast->decls); i++)
                x86_64_generate_code_detail((AST *)vector_get(ast->decls, i));
            return -1;

        case AST_VA_START: {
            int reg = x86_64_generate_code_detail(ast->lhs);
            Code *reg_code = nbyte_reg(8, reg);
            // typedef struct {
            //    unsigned int gp_offset;
            //    unsigned int fp_offset;
            //    void *overflow_arg_area;
            //    void *reg_save_area;
            // } va_list[1];
            Code *gp_offset = addrof(reg_code, 0),
                 *fp_offset = addrof(reg_code, 4),
                 *overflow_arg_area = addrof(reg_code, 8),
                 *reg_save_area = addrof(reg_code, 16);

            assert(ast->rhs->kind == AST_INT);
            appcode(MOVL(value(ast->rhs->ival * 8), gp_offset));
            appcode(MOVL(value(48), fp_offset));
            appcode(LEA(addrof(RBP(), codeenv->overflow_arg_area_stack_idx),
                        RDI()));
            appcode(MOV(RDI(), overflow_arg_area));
            appcode(
                LEA(addrof(RBP(), codeenv->reg_save_area_stack_idx), RDI()));
            appcode(MOV(RDI(), reg_save_area));
            restore_temp_reg(reg);
            return -1;
        }

        case AST_VA_ARG_INT: {
            char *stack_label = make_label_string(),
                 *fetch_label = make_label_string();
            int reg = x86_64_generate_code_detail(ast->lhs);
            Code *reg_code = nbyte_reg(8, reg),
                 *gp_offset = addrof(reg_code, 0),
                 *overflow_arg_area = addrof(reg_code, 8),
                 *reg_save_area = addrof(reg_code, 16);
            appcode(MOV(gp_offset, EAX()));
            appcode(CMP(value(48), EAX()));
            appcode(JAE(stack_label));
            appcode(MOV(EAX(), EDX()));
            appcode(ADD(value(8), EDX()));
            appcode(ADD(reg_save_area, RAX()));
            appcode(MOV(EDX(), gp_offset));
            appcode(JMP(fetch_label));
            appcode(LABEL(stack_label));
            appcode(MOV(overflow_arg_area, RAX()));
            appcode(LEA(addrof(RAX(), 8), RDX()));
            appcode(MOV(RDX(), overflow_arg_area));
            appcode(LABEL(fetch_label));
            appcode(MOV(addrof(RAX(), 0), nbyte_reg(4, reg)));
            return reg;
        }

        case AST_VA_ARG_CHARP: {
            char *stack_label = make_label_string(),
                 *fetch_label = make_label_string();
            int reg = x86_64_generate_code_detail(ast->lhs);
            Code *reg_code = nbyte_reg(8, reg),
                 *gp_offset = addrof(reg_code, 0),
                 *overflow_arg_area = addrof(reg_code, 8),
                 *reg_save_area = addrof(reg_code, 16);
            appcode(MOV(gp_offset, EAX()));
            appcode(CMP(value(48), EAX()));
            appcode(JAE(stack_label));
            appcode(MOV(EAX(), EDX()));
            appcode(ADD(value(8), EDX()));
            appcode(ADD(reg_save_area, RAX()));
            appcode(MOV(EDX(), gp_offset));
            appcode(JMP(fetch_label));
            appcode(LABEL(stack_label));
            appcode(MOV(overflow_arg_area, RAX()));
            appcode(LEA(addrof(RAX(), 8), RDX()));
            appcode(MOV(RDX(), overflow_arg_area));
            appcode(LABEL(fetch_label));
            appcode(MOV(addrof(RAX(), 0), nbyte_reg(8, reg)));
            return reg;
        }

        case AST_GVAR_DECL:
        case AST_LVAR_DECL:
        case AST_FUNC_DECL:
        case AST_NOP:
            return -1;
    }

    warn("gen.c %d\n", ast->kind);
    assert(0);
}

Vector *x86_64_generate_code(Vector *asts)
{
    x86_64_analyze_ast(asts);

    init_code_env();

    appcode(new_code(CD_TEXT));

    for (int i = 0; i < vector_size(asts); i++)
        x86_64_generate_code_detail((AST *)vector_get(asts, i));

    appcode(new_code(CD_DATA));

    Vector *gvar_list = get_gvar_list();
    for (int i = 0; i < vector_size(gvar_list); i++) {
        GVar *gvar = (GVar *)vector_get(gvar_list, i);

        if (gvar->is_global) appcode(GLOBAL(gvar->name));
        appcode(LABEL(gvar->name));

        AST *value = gvar->value;

        if (value == NULL) {  // no initial value
            Code *code = new_code(CD_ZERO);
            code->ival = gvar->type->nbytes;
            appcode(code);
            continue;
        }

        switch (value->kind) {
            case AST_STRING_LITERAL:
                assert(gvar->type->kind == TY_ARY &&
                       gvar->type->ptr_of->kind == TY_CHAR);
                Code *code = new_code(CD_ASCII);
                code->sval = value->sval;
                code->ival = gvar->type->nbytes;
                appcode(code);
                break;

            case AST_CONSTANT: {
                assert(gvar->type->kind != TY_ARY);  // TODO: implement

                // TODO: I wonder if this can be done in
                // x86_64_analyze_ast().
                int ival = x86_64_eval_ast_int(gvar->value);

                int type2spec[16];  // TODO: enough length?
                type2spec[TY_INT] = CD_LONG;
                type2spec[TY_CHAR] = CD_BYTE;
                type2spec[TY_PTR] = CD_QUAD;

                Code *code = new_code(type2spec[gvar->type->kind]);
                code->ival = ival;
                appcode(code);
            } break;

            default:
                assert(0);
        }
    }

    return clone_vector(codeenv->code);
}

int is_register_code(Code *code)
{
    if (code == NULL) return 0;
    return !(code->kind & INST_) &&
           code->kind & (REG_8 | REG_16 | REG_32 | REG_64);
}

int reg_of_nbyte(int nbyte, int reg)
{
    switch (nbyte) {
        case 1:
            return (reg & 31) | REG_8;
        case 2:
            return (reg & 31) | REG_16;
        case 4:
            return (reg & 31) | REG_32;
        case 8:
            return (reg & 31) | REG_64;
    }
    assert(0);
}

Code *nbyte_reg(int nbyte, int reg)
{
    return new_code(reg_of_nbyte(nbyte, reg));
}

int alignment_of(Type *type)
{
    switch (type->kind) {
        case TY_INT:
            return 4;
        case TY_CHAR:
            return 1;
        case TY_PTR:
            return 8;
        case TY_ARY:
            return alignment_of(type->ary_of);
        case TY_UNION:
        case TY_STRUCT: {
            int alignment = 0;
            for (int i = 0; i < vector_size(type->members); i++) {
                StructMember *member =
                    (StructMember *)vector_get(type->members, i);
                alignment = max(alignment, alignment_of(member->type));
            }
            return alignment;
        }
    }
    assert(0);
}