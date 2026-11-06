#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define MAX_LINE 512
#define MAX_OP 200
#define MAX_BYTES 64

typedef struct {
    char name[32];
    char opcode[32];
    char form[64];
    char field[16];
    char imm[16];
} Op;

typedef struct {
    char kind[16];
    char text[128];
    int reg;
    int base;
    int index;
    int scale;
    int disp;
    int has_disp;
    char symbol[128];       /* unresolved/resolved symbolic memory displacement */
} Operand;

typedef struct {
    unsigned char bytes[MAX_BYTES];
    int count;
} Code;

typedef struct {
    char name[128];
    int size;
    char value[256];
    char section;             /* D = .data, B = .bss */
    unsigned int address;     /* offset relative to the section */
    char status;              /* D = defined, U = undefined/external */
    int is_instruction;       /* internal flag: instruction listing row */
} Symbol;

#define MAX_SYMBOLS 1000
Symbol symbols[MAX_SYMBOLS];
int nsymbols = 0;

/* Table of Incomplete Instructions: forward relative references. */
typedef struct {
    char symbol[128];
    unsigned int instruction_address;
    unsigned int patch_offset;
    unsigned int instruction_size;
    int displacement_size;
    int resolved;
} TITEntry;
#define MAX_TIT 2000
static TITEntry tit[MAX_TIT];
static int ntit = 0;

static void tit_add(const char *name, unsigned int address,
                    unsigned int patch_offset, unsigned int instruction_size,
                    int displacement_size) {
    int i;
    for (i = 0; i < ntit; ++i)
        if (!strcmp(tit[i].symbol, name) &&
            tit[i].instruction_address == address) return;
    if (ntit >= MAX_TIT) { fprintf(stderr, "TIT is full\n"); exit(1); }
    snprintf(tit[ntit].symbol, sizeof(tit[ntit].symbol), "%s", name);
    tit[ntit].instruction_address = address;
    tit[ntit].patch_offset = patch_offset;
    tit[ntit].instruction_size = instruction_size;
    tit[ntit].displacement_size = displacement_size;
    tit[ntit].resolved = 0;
    ++ntit;
}

static void write_tit(const char *filename) {
    FILE *fp = fopen(filename, "w");
    int i;
    const int w_addr = 20, w_symbol = 24, w_patch = 14, w_disp = 20, w_status = 14;
    if (!fp) { perror(filename); return; }

    fprintf(fp, "TABLE OF INCOMPLETE INSTRUCTIONS (TIT)\n");
    fprintf(fp, "===============================================================================================\n");
    fprintf(fp, "%-*s %-*s %-*s %-*s %-*s\n",
            w_addr, "INSTRUCTION ADDRESS", w_symbol, "SYMBOL",
            w_patch, "PATCH OFFSET", w_disp, "DISPLACEMENT SIZE", w_status, "STATUS");
    fprintf(fp, "-----------------------------------------------------------------------------------------------\n");
    if (ntit == 0) fprintf(fp, "No incomplete instructions. All references resolved.\n");
    for (i = 0; i < ntit; ++i) {
        char address[32];
        snprintf(address, sizeof(address), "0x%08X", tit[i].instruction_address);
        fprintf(fp, "%-*s %-*s %-*u %-*d %-*s\n",
                w_addr, address, w_symbol, tit[i].symbol,
                w_patch, tit[i].patch_offset, w_disp, tit[i].displacement_size,
                w_status, tit[i].resolved ? "RESOLVED" : "UNRESOLVED");
    }
    fprintf(fp, "===============================================================================================\n");
    fprintf(fp, "Total entries: %d\n", ntit);
    fclose(fp);
}

Op ops[MAX_OP];
int nop = 0;

int regcode[8] = {0,1,2,3,4,5,6,7};

const char *regs[8] = {
    "eax","ecx","edx","ebx",
    "esp","ebp","esi","edi"
};

void trim(char *s) {
    int i = 0;
    int n;
    while (isspace((unsigned char)s[i]))
        i++;
    if (i)
        memmove(s,s+i,strlen(s+i)+1);
    n = strlen(s);
    while (n && isspace((unsigned char)s[n-1]))
        s[--n] = '\0';
}

void lower(char *s) {
    int i;
    for (i=0;s[i];i++)
        s[i] = tolower((unsigned char)s[i]);
}

void remove_spaces(char *s) {
    char t[MAX_LINE];
    int i,j=0;
    for (i=0;s[i];i++) {
        if (!isspace((unsigned char)s[i]))
            t[j++] = s[i];
    }
    t[j] = '\0';
    strcpy(s,t);
}

void remove_comment(char *s) {
    int quote = 0;
    int i;
    for (i=0;s[i];i++) {
        if (s[i] == '"')
            quote = !quote;
        else if (s[i] == ';' && !quote) {
            s[i] = '\0';
            break;
        }
    }
}

int reg_id(const char *s) {
    char t[32];
    int i;

    strcpy(t,s);
    trim(t);
    lower(t);
    for (i=0;i<8;i++) {
        if (!strcmp(t,regs[i]))
            return i;
    }
    return -1;
}

int is_number(const char *s) {
    char *end;
    if (!s || !*s)
        return 0;
    strtol(s,&end,0);
    return *end == '\0';
}

long get_number(const char *s) {
    return strtol(s,NULL,0);
}

void init_operand(Operand *o) {
    memset(o,0,sizeof(Operand));

    o->reg = -1;
    o->base = -1;
    o->index = -1;
    o->scale = 1;
}
int parse_memory(char *s,Operand *o) {
    char t[128];
    char in[128];
    char *l,*r,*token;

    strcpy(t,s);
    trim(t);
    l = strchr(t,'[');
    r = strrchr(t,']');
    if (!l || !r || r <= l)
        return 0;
    *r = '\0';
    strcpy(in,l+1);

    o->base = -1;
    o->index = -1;
    o->scale = 1;
    o->disp = 0;
    o->has_disp = 0;
    for (int i=0;in[i];i++) {
        if (in[i] == '-')
            in[i] = '+';
    }
    token = strtok(in,"+");
    while (token) {
        trim(token);
        if (*token) {
            char *star = strchr(token,'*');
            if (star) {
                int r;
                *star = '\0';
                trim(token);
                trim(star+1);
                r = reg_id(token);
                if (r >= 0 && is_number(star+1)) {
                    o->index = r;
                    o->scale = get_number(star+1);
                }
            }
            else {
                int r = reg_id(token);
                if (r >= 0) {
                    o->base = r;
                }
                else if (is_number(token)) {
                    o->disp = get_number(token);
                    o->has_disp = 1;
                }
                else {
                    snprintf(o->symbol, sizeof(o->symbol), "%s", token);
                    o->disp = 0;
                    o->has_disp = 1;
                }
            }
        }
        token = strtok(NULL,"+");
    }
    strcpy(o->kind,"MEM");
    return 1;
}
void parse_operand(char *s,Operand *o) {
    char t[128];
    char lower_text[128];
    init_operand(o);
    snprintf(t, sizeof(t), "%s", s);
    trim(t);
    snprintf(lower_text, sizeof(lower_text), "%s", t);
    lower(lower_text);
    if (!strncmp(lower_text,"dword",5)) {
        memmove(t,t+5,strlen(t+5)+1);
        trim(t);
    }
    else if (!strncmp(lower_text,"word",4)) {
        memmove(t,t+4,strlen(t+4)+1);
        trim(t);
    }
    else if (!strncmp(lower_text,"byte",4)) {
        memmove(t,t+4,strlen(t+4)+1);
        trim(t);
    }
    snprintf(o->text, sizeof(o->text), "%s", t);
    if (strchr(t,'[')) {
        if (parse_memory(t,o)) return;
    }
    o->reg = reg_id(t);
    if (o->reg >= 0) {
        strcpy(o->kind,"REG");
        return;
    }
    if (is_number(t)) {
        strcpy(o->kind,"IMM");
        return;
    }
    strcpy(o->kind,"SYM");
}

void split_operands(char *s,char *a,char *b) {
    int bracket=0;
    int quote=0;
    int i;
    a[0] = '\0';
    b[0] = '\0';
    for (i=0;s[i];i++) {
        if (s[i] == '"')
            quote = !quote;
        else if (!quote) {
            if (s[i] == '[')
                bracket++;
            if (s[i] == ']')
                bracket--;
            if (s[i] == ',' && bracket == 0) {
                s[i] = '\0';
                strcpy(a,s);
                strcpy(b,s+i+1);
                trim(a);
                trim(b);
                return;
            }
        }
    }
    strcpy(a,s);
    trim(a);
}
int parse_instruction(char *line,char *mn,char *a,char *b) {
    char t[MAX_LINE];
    char rest[MAX_LINE] = "";
    char *colon;
    mn[0] = '\0';
    a[0] = '\0';
    b[0] = '\0';
    strcpy(t,line);
    trim(t);
    if (!t[0])
        return 0;
    colon = strchr(t,':');
    if (colon) {
        memmove(t,colon+1,strlen(colon+1)+1);
        trim(t);
    }
    if (sscanf(t,"%31s %511[^\n]",mn,rest) < 1)
        return 0;
    lower(mn);
    if (rest[0])
        split_operands(rest,a,b);
    return 1;
}
void load_instruction_set(const char *file) {
    FILE *fp;
    char line[MAX_LINE];
    fp = fopen(file,"r");
    if (!fp) {
        perror(file);
        exit(1);
    }
    while (fgets(line,sizeof(line),fp)) {
        char name[32],opcode[32],form[64];
        char c4[32],c5[32],c6[32],field[16],imm[16];
        char bits[32],regname[32];
        int n;

        trim(line);
        if (!line[0])
            continue;
        if (sscanf(line,"| %31[^|] | %31[^|] |",bits,regname) == 2) {
            trim(bits);
            trim(regname);
            lower(regname);
            if (strlen(bits)==3 && (bits[0]=='0'||bits[0]=='1') && (bits[1]=='0'||bits[1]=='1') && (bits[2]=='0'||bits[2]=='1')) {
                int r = reg_id(regname);
                if (r >= 0)
                    regcode[r] = strtol(bits,NULL,2);
                continue;
            }
        }
        n = sscanf(line,"| %31[^|] | %31[^|] | %63[^|] | %31[^|] | %31[^|] | %31[^|] | %15[^|] | %15[^|] |",name,opcode,form,c4,c5,c6,field,imm);
        if (n == 8) {
            trim(name);
            trim(opcode);
            trim(form);
            trim(field);
            trim(imm);
            lower(name);
            remove_spaces(form);
            if (!strcmp(name,"instruction"))
                continue;
            if (nop < MAX_OP) {
                strcpy(ops[nop].name,name);
                strcpy(ops[nop].opcode,opcode);
                strcpy(ops[nop].form,form);
                strcpy(ops[nop].field,field);
                strcpy(ops[nop].imm,imm);
                nop++;
            }
        }
    }
    fclose(fp);
}
int value_operand(Operand *o) {
    return !strcmp(o->kind,"IMM") || !strcmp(o->kind,"SYM");
}

int match_form(const Op *op,Operand *a,Operand *b,int two) {
    char form[64];
    strcpy(form,op->form);
    trim(form);
    remove_spaces(form);
    lower(form);

    if (!two) {
        if (!strcmp(form,"-"))
            return 1;

        if (!strcmp(form,"r32"))
            return !strcmp(a->kind,"REG");

        if (!strcmp(form,"r/m32"))
            return !strcmp(a->kind,"REG") || !strcmp(a->kind,"MEM");

        if (!strcmp(form,"imm8") || !strcmp(form,"imm32"))
            return value_operand(a);

        if (!strcmp(form,"rel8") || !strcmp(form,"rel32"))
            return value_operand(a);
        return 0;
    }

    if (!strcmp(form,"r/m32,r32"))
        return (!strcmp(a->kind,"REG") || !strcmp(a->kind,"MEM")) && !strcmp(b->kind,"REG");

    if (!strcmp(form,"r32,r/m32"))
        return !strcmp(a->kind,"REG") && (!strcmp(b->kind,"REG") || !strcmp(b->kind,"MEM"));

    if (!strcmp(form,"r/m32,imm8"))
        return (!strcmp(a->kind,"REG") || !strcmp(a->kind,"MEM")) && value_operand(b);

    if (!strcmp(form,"r/m32,imm32"))
        return (!strcmp(a->kind,"REG") || !strcmp(a->kind,"MEM")) && value_operand(b);

    if (!strcmp(form,"r32,imm8"))
        return !strcmp(a->kind,"REG") && value_operand(b);

    if (!strcmp(form,"r32,imm32"))
        return !strcmp(a->kind,"REG") && value_operand(b);

    if (!strcmp(form,"eax,imm32"))
        return !strcmp(a->kind,"REG") && a->reg == 0 && value_operand(b);
    return 0;
}
int fits_imm8(Operand *o) {
    long v;
    if (strcmp(o->kind,"IMM"))
        return 0;
    v = get_number(o->text);
    return v >= -128 && v <= 127;
}
const Op *find_instruction(const char *mn,Operand *a,Operand *b,int two) {
    int i;
    for (i=0;i<nop;i++) {
        if (!strcmp(ops[i].name,mn) && strstr(ops[i].opcode,"+rd") && match_form(&ops[i],a,b,two))
            return &ops[i];
    }
    if (two && fits_imm8(b)) {
        for (i=0;i<nop;i++) {
            if (!strcmp(ops[i].name,mn) && strstr(ops[i].form,"imm8") && match_form(&ops[i],a,b,two))
                return &ops[i];
        }
    }
    for(i=0;i<nop;i++) {
        if (!strcmp(ops[i].name,mn) && match_form(&ops[i],a,b,two))
            return &ops[i];
    }
    return NULL;
}
void emit_byte(Code *c,unsigned int v) {
    if (c->count < MAX_BYTES)
        c->bytes[c->count++] = v & 0xff;
}
void emit_le(Code *c,unsigned long v,int size) {
    int i;
    for (i=0;i<size;i++)
        emit_byte(c,(v >> (8*i)) & 0xff);
}
void emit_opcode(const char *opcode,Code *c) {
    char t[32];
    char *plus;
    int i;
    strcpy(t,opcode);
    trim(t);
    plus = strstr(t,"+rd");
    if (plus) {
        char base[16];
        int n = plus-t;
        strncpy(base,t,n);
        base[n] = '\0';
        emit_byte(c,strtoul(base,NULL,16));
        return;
    }
    for (i=0;t[i];i+=2) {
        char x[3];
        x[0] = t[i];
        x[1] = t[i+1];
        x[2] = '\0';
        emit_byte(c,strtoul(x,NULL,16));
    }
}
void memory_modrm(Operand *o,int *mod,int *rm,int *has_sib,int *sib,int *disp_size) {
    int base;
    int index;
    int scale;
    *has_sib = 0;
    *sib = 0;
    if (o->base < 0) {
        *mod = 0;
        *rm = 5;
        *disp_size = 4;
        return;
    }
    if (o->base == 4 || o->index >= 0) {
        *has_sib = 1;
        base = o->base;
        index = o->index;
        if (base < 0)
            base = 5;
        if (index < 0)
            index = 4;
        if (o->scale == 1)
            scale = 0;
        else if (o->scale == 2)
            scale = 1;
        else if (o->scale == 4)
            scale = 2;
        else
            scale = 3;
        *sib = (scale<<6) | (index<<3) | base;
        *rm = 4;
    }
    else {
        *rm = o->base;
    }
    if (!o->has_disp) {
        if(o->base == 5) {
            *mod = 1;
            *disp_size = 1;
        }
        else {
            *mod = 0;
            *disp_size = 0;
        }
        return;
    }
    if(o->disp >= -128 &&
        o->disp <= 127) {
        *mod = 1;
        *disp_size = 1;
    }
    else {
        *mod = 2;
        *disp_size = 4;
    }
}
int reg_field(const Op *op,
              Operand *a,
              Operand *b) {
    if (op->field[0] == '/' &&
        isdigit((unsigned char)op->field[1]))
        return op->field[1]-'0';
    if (!strcmp(op->form,"r/m32,r32"))
        return b->reg;
    if (!strcmp(op->form,"r32,r/m32"))
        return a->reg;
    return 0;
}
void make_modrm(const Op *op,Operand *a,Operand *b,Code *c){
    Operand *rmop;
    int reg;
    int mod,rm;
    int has_sib,sib;
    int disp_size;
    reg = reg_field(op,a,b);
    if(!strcmp(op->form,"r/m32,r32"))
        rmop = a;
    else if (!strcmp(op->form,"r32,r/m32"))
        rmop = b;
    else
        rmop = a;
    if(!strcmp(rmop->kind,"REG")) {
        mod = 3;
        rm = rmop->reg;
        has_sib = 0;
        sib = 0;
        disp_size = 0;
    }
    else {
        memory_modrm(rmop,&mod,&rm,&has_sib,&sib,&disp_size);
    }
    emit_byte(c,(mod<<6) | (reg<<3) |rm);
    if(has_sib)
        emit_byte(c,sib);
    if(disp_size == 1)
        emit_byte(c,rmop->disp);
    else if (disp_size == 4)
        emit_le(c,rmop->disp,4);
}
static Symbol *lookup_symbol(const char *name) {
    int i;
    for (i = 0; i < nsymbols; ++i)
        if (!strcasecmp(symbols[i].name, name) && symbols[i].status == 'D')
            return &symbols[i];
    return NULL;
}

static void resolve_operand_symbols(Operand *o) {
    Symbol *sym;
    if (!o) return;
    if (!strcmp(o->kind, "MEM") && o->symbol[0]) {
        sym = lookup_symbol(o->symbol);
        if (sym) {
            o->disp = (int)sym->address;
            o->has_disp = 1;
        }
    }
}

unsigned long immediate_value(Operand *o) {
    if (!strcmp(o->kind, "IMM"))
        return get_number(o->text);
    if (!strcmp(o->kind, "SYM")) {
        Symbol *sym = lookup_symbol(o->text);
        if (!sym) return 0;
        /* EQU constants have no allocated storage; data/text symbols use addresses. */
        if (sym->size == 0 && strcmp(sym->value, "{}") && is_number(sym->value))
            return (unsigned long)get_number(sym->value);
        return sym->address;
    }
    return 0;
}
void encode(const Op *op,Operand *a,Operand *b,Code *c) {
    int two = b && b->kind[0];
    Operand *v;
    if(strstr(op->opcode,"+rd")) {
        char base[16];
        char *p;
        int n;
        strcpy(base,op->opcode);
        p = strstr(base,"+rd");
        n = p-base;
        base[n] = '\0';
        emit_byte(c,strtoul(base,NULL,16) + regcode[a->reg]);
        if(strstr(op->form,"imm32"))
            emit_le(c,immediate_value(b),4);
        else if(strstr(op->form,"imm8"))
            emit_byte(c,immediate_value(b));
        return;
    }
    emit_opcode(op->opcode,c);
    if(strstr(op->form,"r/m32"))
        make_modrm(op,a,b,c);
    if(strstr(op->imm,"imm8")) {
        v = two ? b : a;
        emit_byte(c,immediate_value(v));
        return;
    }
    if(strstr(op->imm,"imm32")) {
        v = two ? b : a;
        emit_le(c,immediate_value(v),4);
        return;
    }
    if(strstr(op->imm,"rel8")){
        emit_byte(c,0);
        return;
    }
    if(strstr(op->imm,"rel32")){
        emit_le(c,0,4);
        return;
    }
}

/* Resolve a relative label operand after pass 1 has collected all labels. */
static void resolve_relative_operand(const Op *op, Operand *target,
                                     unsigned int instruction_address, Code *code) {
    Symbol *sym;
    long displacement;
    int width, i, offset;
    if (!op || !target || strcmp(target->kind, "SYM") || code->count == 0)
        return;
    if (strstr(op->imm, "rel8")) width = 1;
    else if (strstr(op->imm, "rel32")) width = 4;
    else return;
    sym = lookup_symbol(target->text);
    if (!sym || sym->section != 'T') return; /* external relocation remains unresolved */
    displacement = (long)sym->address - ((long)instruction_address + code->count);
    offset = code->count - width;
    if (offset < 0) return;
    for (i = 0; i < width; ++i)
        code->bytes[offset + i] = (unsigned char)(((unsigned long)displacement >> (8 * i)) & 0xff);
    /* The forward reference is now patched; remove its TIT entry. */
    for (i = 0; i < ntit; ++i) {
        if (!strcmp(tit[i].symbol, target->text) &&
            tit[i].instruction_address == instruction_address) {
            memmove(&tit[i], &tit[i + 1],
                    (size_t)(ntit - i - 1) * sizeof(tit[0]));
            --ntit;
            break;
        }
    }
}

int is_data_directive(const char *s) {
    return !strcmp(s,"db") || !strcmp(s,"dw") || !strcmp(s,"dd") || !strcmp(s,"resb") || !strcmp(s,"resw") || !strcmp(s,"resd");
}

/* Return the storage width of a NASM data/reservation directive. */
int directive_width(const char *dir) {
    if (!strcmp(dir, "db") || !strcmp(dir, "resb")) return 1;
    if (!strcmp(dir, "dw") || !strcmp(dir, "resw")) return 2;
    if (!strcmp(dir, "dd") || !strcmp(dir, "resd")) return 4;
    return 0;
}

/* Count comma-separated values, respecting quoted strings. */
int data_item_count(const char *text) {
    int count = 0, quote = 0, has_item = 0;
    const char *p;
    for (p = text; *p; ++p) {
        if (*p == '\"') quote = !quote;
        if (!quote && *p == ',') {
            if (has_item) count++;
            has_item = 0;
        } else if (!isspace((unsigned char)*p)) {
            has_item = 1;
        }
    }
    if (has_item) count++;
    return count;
}

/* Keep declared initial values readable in the symbol table, e.g. 4,8. */
void format_symbol_value(const char *dir, const char *rest, char *out, size_t out_size) {
    char temp[MAX_LINE];
    size_t j = 0;
    int quote = 0;
    const char *p;
    if (!strncmp(dir, "res", 3)) {
        snprintf(out, out_size, "{}");
        return;
    }
    snprintf(temp, sizeof(temp), "%s", rest);
    trim(temp);
    for (p = temp; *p && j + 1 < out_size; ++p) {
        if (*p == '\"') quote = !quote;
        if (!isspace((unsigned char)*p) || quote)
            out[j++] = *p;
    }
    out[j] = '\0';
    if (!out[0]) snprintf(out, out_size, "{}");
}

/* Add a symbol that is declared external or referenced but not defined yet. */
void add_undefined_symbol(const char *name) {
    int i;
    if (!name || !name[0]) return;

    for (i = 0; i < nsymbols; ++i) {
        if (!strcmp(symbols[i].name, name))
            return; /* Keep an existing definition, if one already exists. */
    }
    if (nsymbols >= MAX_SYMBOLS) return;

    snprintf(symbols[nsymbols].name, sizeof(symbols[nsymbols].name), "%s", name);
    symbols[nsymbols].size = 0;
    snprintf(symbols[nsymbols].value, sizeof(symbols[nsymbols].value), "{}");
    symbols[nsymbols].section = '-'; /* Not defined in a local section. */
    symbols[nsymbols].address = 0;
    symbols[nsymbols].status = 'U';
    symbols[nsymbols].is_instruction = 0;
    nsymbols++;
}

void add_data_symbol(const char *name, const char *dir, const char *rest,
                     char section, unsigned int address) {
    int width = directive_width(dir);
    int size;
    int i;
    char value[256];
    if (!name[0] || !width) return;

    /* If an earlier reference marked this symbol U, replace it with its definition. */
    for (i = 0; i < nsymbols; ++i) {
        if (!strcmp(symbols[i].name, name)) {
            if (symbols[i].status == 'D') return;
            break;
        }
    }
    if (i == nsymbols && nsymbols >= MAX_SYMBOLS) return;

    if (!strncmp(dir, "res", 3)) {
        long n = get_number(rest);
        if (n < 0) n = 0;
        size = (int)(n * width);
    } else if (!strcmp(dir, "db") && strchr(rest, '\"')) {
        /* Strings occupy their character count; comma-separated items are also supported. */
        size = 0;
        {
            const char *p = rest;
            while (*p) {
                if (*p == '\"') {
                    ++p;
                    while (*p && *p != '\"') { size++; ++p; }
                    if (*p) ++p;
                } else if (*p == ',') {
                    ++p;
                } else if (!isspace((unsigned char)*p)) {
                    char token[128];
                    int k = 0;
                    while (*p && *p != ',' && !isspace((unsigned char)*p)) {
                        if (k < (int)sizeof(token)-1) token[k++] = *p;
                        ++p;
                    }
                    token[k] = '\0';
                    if (is_number(token)) size += width;
                } else ++p;
            }
        }
    } else {
        size = data_item_count(rest) * width;
    }
    format_symbol_value(dir, rest, value, sizeof(value));
    if (i < nsymbols) {
        /* Convert an existing U entry into a defined symbol without changing its ID. */
        snprintf(symbols[i].name, sizeof(symbols[i].name), "%s", name);
        symbols[i].size = size;
        snprintf(symbols[i].value, sizeof(symbols[i].value), "%s", value);
        symbols[i].section = section;
        symbols[i].address = address;
        symbols[i].status = 'D';
    } else {
        snprintf(symbols[nsymbols].name, sizeof(symbols[nsymbols].name), "%s", name);
        symbols[nsymbols].size = size;
        snprintf(symbols[nsymbols].value, sizeof(symbols[nsymbols].value), "%s", value);
        symbols[nsymbols].section = section;
        symbols[nsymbols].address = address;
        symbols[nsymbols].status = 'D';
        symbols[nsymbols].is_instruction = 0;
        nsymbols++;
    }
}

void collect_data_symbol(const char *source_line, char section,
                         unsigned int address) {
    char t[MAX_LINE], first[128] = "", second[128] = "", rest[MAX_LINE] = "";
    char *colon;
    snprintf(t, sizeof(t), "%s", source_line);
    trim(t);
    colon = strchr(t, ':');
    if (colon) {
        *colon = '\0';
        trim(t);
        if (t[0]) {
            char label[128];
            snprintf(label, sizeof(label), "%s", t);
            trim(colon + 1);
            if (sscanf(colon + 1, "%127s %511[^\n]", second, rest) == 2) {
                lower(second);
                if (is_data_directive(second)) add_data_symbol(label, second, rest, section, address);
            } else if (sscanf(colon + 1, "%127s", second) == 1) {
                lower(second);
                if (is_data_directive(second)) add_data_symbol(label, second, "", section, address);
            }
            return;
        }
    }
    if (sscanf(t, "%127s %127s %511[^\n]", first, second, rest) >= 2) {
        lower(second);
        if (is_data_directive(second)) add_data_symbol(first, second, rest, section, address);
    }
}

/* Add a label in the text section to the combined symbol/instruction table. */
void add_text_label(const char *name, unsigned int address) {
    int i;
    if (!name || !name[0]) return;
    for (i = 0; i < nsymbols; ++i) {
        if (!strcmp(symbols[i].name, name)) {
            if (symbols[i].status == 'U') {
                symbols[i].size = 0;
                snprintf(symbols[i].value, sizeof(symbols[i].value), "{}");
                symbols[i].section = 'T';
                symbols[i].address = address;
                symbols[i].status = 'D';
                symbols[i].is_instruction = 0;
            }
            return;
        }
    }
    if (nsymbols >= MAX_SYMBOLS) return;
    snprintf(symbols[nsymbols].name, sizeof(symbols[nsymbols].name), "%s", name);
    symbols[nsymbols].size = 0;
    snprintf(symbols[nsymbols].value, sizeof(symbols[nsymbols].value), "{}");
    symbols[nsymbols].section = 'T';
    symbols[nsymbols].address = address;
    symbols[nsymbols].status = 'D';
    symbols[nsymbols].is_instruction = 0;
    nsymbols++;
}

/* EQU symbols are constants: size is zero because they allocate no storage. */
void collect_equ_symbol(const char *line, char section, unsigned int current_address) {
    char t[MAX_LINE], name[128] = "", directive[32] = "", expr[256] = "";
    char value[256] = "{}";
    char *colon;
    long result = 0;
    int found = 0, i;
    snprintf(t, sizeof(t), "%s", line);
    trim(t);
    colon = strchr(t, ':');
    if (colon) { /* labels may also precede an EQU directive */
        *colon = '\0';
        trim(t);
        snprintf(name, sizeof(name), "%s", t);
        snprintf(t, sizeof(t), "%s", colon + 1);
        trim(t);
        if (sscanf(t, "%31s %255[^\n]", directive, expr) < 1) return;
    } else {
        if (sscanf(t, "%127s %31s %255[^\n]", name, directive, expr) < 2) return;
    }
    lower(directive);
    if (strcmp(directive, "equ") != 0 || !name[0]) return;
    trim(expr);
    /* Support the common NASM expression used here: EQU $-previous_symbol. */
    if (expr[0] == '$' && expr[1] == '-') {
        char target[128];
        snprintf(target, sizeof(target), "%s", expr + 2);
        trim(target);
        for (i = 0; i < nsymbols; ++i) {
            if (!strcmp(symbols[i].name, target) && symbols[i].status == 'D') {
                result = (long)current_address - (long)symbols[i].address;
                found = 1;
                break;
            }
        }
    } else if (is_number(expr)) {
        result = get_number(expr);
        found = 1;
    }
    if (found) snprintf(value, sizeof(value), "%ld", result);
    for (i = 0; i < nsymbols; ++i) {
        if (!strcmp(symbols[i].name, name)) {
            snprintf(symbols[i].value, sizeof(symbols[i].value), "%s", value);
            symbols[i].size = 0;
            symbols[i].section = section;
            symbols[i].address = current_address;
            symbols[i].status = 'D';
            symbols[i].is_instruction = 0;
            return;
        }
    }
    if (nsymbols >= MAX_SYMBOLS) return;
    snprintf(symbols[nsymbols].name, sizeof(symbols[nsymbols].name), "%s", name);
    symbols[nsymbols].size = 0;
    snprintf(symbols[nsymbols].value, sizeof(symbols[nsymbols].value), "%s", value);
    symbols[nsymbols].section = section;
    symbols[nsymbols].address = current_address;
    symbols[nsymbols].status = 'D';
    symbols[nsymbols].is_instruction = 0;
    nsymbols++;
}

/* Add one row for every source instruction, including its emitted machine bytes. */
void add_instruction_entry(const char *source, const Code *code, unsigned int address) {
    char name[128], value[256];
    int i, n = 0;
    snprintf(name, sizeof(name), "%s", source);
    trim(name);
    if (!name[0]) return;
    for (i = 0; i < code->count && n + 2 < (int)sizeof(value); ++i)
        n += snprintf(value + n, sizeof(value) - (size_t)n, "%02X", code->bytes[i]);
    if (code->count == 0) snprintf(value, sizeof(value), "??");
    if (nsymbols >= MAX_SYMBOLS) return;
    snprintf(symbols[nsymbols].name, sizeof(symbols[nsymbols].name), "%s", name);
    symbols[nsymbols].size = code->count;
    snprintf(symbols[nsymbols].value, sizeof(symbols[nsymbols].value), "%s", value);
    symbols[nsymbols].section = 'T';
    symbols[nsymbols].address = address;
    symbols[nsymbols].status = 'D';
    symbols[nsymbols].is_instruction = 1;
    nsymbols++;
}

void write_symbol_table(const char *filename) {
    FILE *fp = fopen(filename, "w");
    int i;
    int w_id = 4, w_name = 12, w_size = 6, w_value = 12;
    int w_section = 9, w_address = 10, w_status = 8;
    if (!fp) { perror(filename); return; }

    /* Expand columns to fit the longest value; never truncate symbol values. */
    for (i = 0; i < nsymbols; ++i) {
        int n;
        n = (int)strlen(symbols[i].name); if (n > w_name) w_name = n;
        n = (int)strlen(symbols[i].value); if (n > w_value) w_value = n;
    }

    fprintf(fp, "SYMBOL TABLE\n");
    fprintf(fp, "================================================================================================================\n");
    fprintf(fp, "%-*s %-*s %-*s %-*s %-*s %-*s %-*s\n",
            w_id, "ID", w_name, "NAME", w_size, "SIZE", w_value, "VALUE",
            w_section, "SECTION", w_address, "ADDRESS", w_status, "STATUS");
    fprintf(fp, "----------------------------------------------------------------------------------------------------------------\n");

    if (nsymbols == 0) fprintf(fp, "No symbols found.\n");
    for (i = 0; i < nsymbols; ++i) {
        char section[2], status[2];
        section[0] = symbols[i].section ? symbols[i].section : '-'; section[1] = '\0';
        status[0] = symbols[i].status ? symbols[i].status : '-'; status[1] = '\0';
        fprintf(fp, "%-*d %-*s %-*d %-*s %-*s 0x%-*X %-*s\n",
                w_id, i + 1, w_name, symbols[i].name, w_size, symbols[i].size,
                w_value, symbols[i].value, w_section, section,
                w_address - 2, symbols[i].address, w_status, status);
    }
    fprintf(fp, "================================================================================================================\n");
    fprintf(fp, "Total entries: %d\n", nsymbols);
    fclose(fp);
}
void emit_data(const char *dir,char *rest,Code *c){
    int size;
    char *p = rest;
    if(!strcmp(dir,"db"))
        size = 1;
    else if(!strcmp(dir,"dw"))
        size = 2;
    else
        size = 4;
    if(!strcmp(dir,"resb") || !strcmp(dir,"resw") || !strcmp(dir,"resd"))
        return;

    while(*p){
        while(isspace((unsigned char)*p) || *p == ',')
            p++;
        if(!*p)
            break;
        if(*p == '"') {
            p++;
            while (*p && *p != '"') {
                emit_byte(c,*p);
                p++;
            }
            if(*p)
                p++;
            continue;
        }
        {
            char token[128];
            int i=0;
            while (*p && *p != ',') {
                if (i < 127)
                    token[i++] = *p;
                p++;
            }
            token[i] = '\0';
            trim(token);
            if (is_number(token))
                emit_le(c,get_number(token),size);
        }
    }
}

int process_data(char *line,Code *c) {
    char t[MAX_LINE];
    char first[64];
    char second[64];
    char rest[MAX_LINE];
    strcpy(t,line);
    trim(t);
    {
        char *colon = strchr(t,':');
        if (colon) {
            memmove(t,colon+1,strlen(colon+1)+1);
            trim(t);
        }
    }
    if (sscanf(t,
               "%63s %63s %511[^\n]",
               first,
               second,
               rest) == 3) {
        lower(first);
        lower(second);
        if (is_data_directive(second)) {
            if (strcmp(second,"resb") && strcmp(second,"resw") && strcmp(second,"resd"))
                emit_data(second,rest,c);
            return 1;
        }
    }
    if (sscanf(t,"%63s %511[^\n]",first,rest) == 2){
        lower(first);
        if (is_data_directive(first)){
            if (strcmp(first,"resb") && strcmp(first,"resw") && strcmp(first,"resd"))
                emit_data(first,rest,c);
            return 1;
        }
    }
    return 0;
}
void print_code(Code *c,unsigned int address) {
    int i;
    if (!c->count) {
        printf("??");
        return;
    }
    for (i=0;i<c->count;i++) {
        if (i && i%8==0)
            printf("\n%08X  ",address+i);
        printf("%02X",c->bytes[i]);
    }
}

/* Print listing rows using fixed-width ADDRESS, HEX BYTES and SOURCE columns. */
static void print_listing_row(unsigned int address, const Code *code,
                              const char *source) {
    char addr[32];
    int i, start = 0;
    snprintf(addr, sizeof(addr), "%08X", address);

    if (!code || code->count <= 0) {
        const char *empty_bytes = code ? "??" : "--";
        printf("%-10s %-34s %s", addr, empty_bytes, source ? source : "");
        if (!source || !strchr(source, '\n')) putchar('\n');
        return;
    }

    while (start < code->count) {
        char bytes[33];
        int n = 0, end = start + 16;
        if (end > code->count) end = code->count;
        for (i = start; i < end; ++i)
            n += snprintf(bytes + n, sizeof(bytes) - (size_t)n,
                          "%02X", code->bytes[i]);
        bytes[n] = '\0';
        if (start == 0) {
            printf("%-10s %-34s %s", addr, bytes, source ? source : "");
            if (!source || !strchr(source, '\n')) putchar('\n');
        } else {
            printf("%-10s %-34s\n", "", bytes);
        }
        start = end;
    }
}

unsigned int calculate_address(unsigned int base,int index,int size){
    return base + index * size;
}
int reserved_size(char *line){
    char t[MAX_LINE];
    char first[64];
    char second[64];
    char rest[MAX_LINE];
    long n;
    int size=0;
    strcpy(t,line);
    trim(t);
    {
        char *colon = strchr(t,':');
        if (colon) {
            memmove(t,colon+1,strlen(colon+1)+1);
            trim(t);
        }
    }
    if (sscanf(t,"%63s %63s %511[^\n]",first,second,rest) == 3) {
        lower(first);
        lower(second);
        if (!strcmp(second,"resb"))
            size=1;
        else if (!strcmp(second,"resw"))
            size=2;
        else if (!strcmp(second,"resd"))
            size=4;
        if (size) {
            n=get_number(rest);
            return n*size;
        }
    }
    if (sscanf(t,"%63s %511[^\n]",first,rest) == 2) {
        lower(first);
        if (!strcmp(first,"resb"))
            size=1;
        else if (!strcmp(first,"resw"))
            size=2;
        else if (!strcmp(first,"resd"))
            size=4;
        if (size) {
            n=get_number(rest);
            return n*size;
        }
    }
    return 0;
}
/* PASS 1: determine section-relative addresses and collect every definition.
 * Instruction encoding is used only to determine instruction lengths here;
 * no listing is printed and no instruction rows are added to the symbol table.
 */
static void first_pass(const char *file) {
    FILE *fp = fopen(file, "r");
    char line[MAX_LINE], section[32] = "";
    unsigned int section_address = 0;
    if (!fp) { perror(file); exit(1); }

    while (fgets(line, sizeof(line), fp)) {
        char work[MAX_LINE], *colon;
        remove_comment(line);
        trim(line);
        if (!line[0]) continue;
        if (!strncmp(line, "section", 7)) {
            sscanf(line + 7, "%31s", section);
            lower(section);
            section_address = 0;
            continue;
        }
        if (!strncmp(line, "global ", 7)) continue;
        if (!strncmp(line, "extern ", 7)) {
            char names[MAX_LINE], *tok;
            snprintf(names, sizeof(names), "%s", line + 7);
            tok = strtok(names, ", \t");
            while (tok) { add_undefined_symbol(tok); tok = strtok(NULL, ", \t"); }
            continue;
        }
        if (!strcmp(section, ".data") || !strcmp(section, ".bss")) {
            Code data = {0};
            char sec = !strcmp(section, ".data") ? 'D' : 'B';
            collect_data_symbol(line, sec, section_address);
            if (process_data(line, &data)) {
                if (data.count) section_address += (unsigned int)data.count;
                else section_address += (unsigned int)reserved_size(line);
                collect_equ_symbol(line, sec, section_address);
            } else {
                collect_equ_symbol(line, sec, section_address);
            }
            continue;
        }
        if (strcmp(section, ".text")) continue;

        snprintf(work, sizeof(work), "%s", line);
        colon = strchr(work, ':');
        if (colon) {
            char label[128];
            size_t n = (size_t)(colon - work);
            if (n >= sizeof(label)) n = sizeof(label) - 1;
            memcpy(label, work, n); label[n] = '\0'; trim(label);
            add_text_label(label, section_address);
            memmove(work, colon + 1, strlen(colon + 1) + 1);
            trim(work);
            if (!work[0]) continue;
        }
        {
            char mn[64], at[256], bt[256];
            Operand a, b;
            const Op *op;
            Code code = {0};
            if (!parse_instruction(work, mn, at, bt)) continue;
            parse_operand(at, &a);
            if (bt[0]) parse_operand(bt, &b); else init_operand(&b);
            resolve_operand_symbols(&a);
            resolve_operand_symbols(&b);
            if (!strcmp(a.kind, "SYM")) add_undefined_symbol(a.text);
            if (bt[0] && !strcmp(b.kind, "SYM")) add_undefined_symbol(b.text);
            op = find_instruction(mn, &a, &b, bt[0] != '\0');
            if (op) {
                encode(op, &a, &b, &code);
                /* Relative symbol operands are tracked in TIT, then resolved
                   after all labels have been collected by Pass 1. */
                if (!strcmp(a.kind, "SYM") &&
                    (strstr(op->imm, "rel8") || strstr(op->imm, "rel32"))) {
                    int width = strstr(op->imm, "rel8") ? 1 : 4;
                    tit_add(a.text, section_address,
                            (unsigned int)(code.count - width),
                            (unsigned int)code.count, width);
                }
                section_address += (unsigned int)code.count;
            }
        }
    }
    /* Resolve TIT entries after every label has been collected. */
    {
        int i;
        for (i = 0; i < ntit; ++i) {
            Symbol *sym = lookup_symbol(tit[i].symbol);
            if (sym && sym->status == 'D' && sym->section == 'T')
                tit[i].resolved = 1;
        }
    }
    fclose(fp);
    write_tit("TIT.txt");
}

void process_file(const char *file, const char *object_file){
    FILE *fp;
    FILE *obj;
    char line[MAX_LINE];
    char section[32]="";
    int section_address=0;
    obj = fopen(object_file, "wb");
    if (!obj) { perror(object_file); exit(1); }
    fp = fopen(file,"r");
    if (!fp){
        perror(file);
        fclose(obj);
        exit(1);
    }
    printf("\n%-10s %-34s %s\n", "ADDRESS", "HEX BYTES", "SOURCE");
    printf("--------------------------------------------------------------------------\n");
    while (fgets(line,sizeof(line),fp)){
        char original[MAX_LINE];
        strcpy(original,line);
        remove_comment(line);
        trim(line);
        if (!line[0])
            continue;
        if (!strncmp(line,"section",7)){
            sscanf(line+7,"%31s",section);
            lower(section);
            section_address=0;
            printf("\n%s\n",section);
            continue;
        }
        if (!strncmp(line,"global ",7) || !strncmp(line,"extern ",7)){
            if (!strncmp(line, "extern ", 7)) {
                char names[MAX_LINE];
                char *token;
                snprintf(names, sizeof(names), "%s", line + 7);
                token = strtok(names, ", \t");
                while (token) {
                    add_undefined_symbol(token);
                    token = strtok(NULL, ", \t");
                }
            }
            print_listing_row(0, NULL, original);
            continue;
        }
        if (!strcmp(section,".data") || !strcmp(section,".bss")){
            Code c={0};
            unsigned int address=calculate_address(0,section_address,1);
            /* Symbol definitions and EQU values were collected in pass 1. */
            if (process_data(line,&c)){
                if (c.count){
                    print_listing_row(address, &c, original);
                    if (!strcmp(section, ".data")) fwrite(c.bytes, 1, (size_t)c.count, obj);
                    section_address += c.count;
                }
                else{
                    int size=reserved_size(line);
                    int z;
                    print_listing_row(address, NULL, original);
                    if (!strcmp(section, ".bss"))
                        for (z = 0; z < size; ++z) fputc(0, obj);
                    section_address += size;
                }
            }
            continue;
        }
        {
            char mn[64];
            char atext[256];
            char btext[256];
            char temp[MAX_LINE];
            char *colon;
            char *after;
            Operand a,b;
            Code c={0};
            const Op *op;
            unsigned int address=calculate_address(0,section_address,1);
            strcpy(temp,line);
            colon=strchr(temp,':');
            if (colon){
                after=colon+1;
                trim(after);
                if (!after[0]){
                    *colon='\0';
                    trim(temp);
                    add_text_label(temp, address);
                    print_listing_row(address, NULL, original);
                    continue;
                }
                {
                    char label_name[128];
                    size_t label_len = (size_t)(colon - temp);
                    if (label_len >= sizeof(label_name)) label_len = sizeof(label_name) - 1;
                    memcpy(label_name, temp, label_len);
                    label_name[label_len] = '\0';
                    trim(label_name);
                    add_text_label(label_name, address);
                }
            }
            if (!parse_instruction(line,mn,atext,btext))
                continue;
            parse_operand(atext,&a);
            if (btext[0])
                parse_operand(btext,&b);
            else
                init_operand(&b);
            resolve_operand_symbols(&a);
            resolve_operand_symbols(&b);
            /* Collect symbol references for U status without changing encoding. */
            if (!strcmp(a.kind, "SYM")) add_undefined_symbol(a.text);
            if (btext[0] && !strcmp(b.kind, "SYM")) add_undefined_symbol(b.text);
            op = find_instruction(mn,&a,&b,btext[0] != '\0');
            if(op){
                encode(op,&a,&b,&c);
                resolve_relative_operand(op, &a, address, &c);
                if (c.count) fwrite(c.bytes, 1, (size_t)c.count, obj);
                section_address += c.count;
            }
            add_instruction_entry(line, &c, address);
            if (!op) {
                Code unknown = {0};
                print_listing_row(address, &unknown, original);
            } else {
                print_listing_row(address, &c, original);
            }
        }
    }
    fclose(fp);
    fclose(obj);
    write_symbol_table("symbol_table.txt");
    write_tit("TIT.txt");
}
int main(int argc,char *argv[]){
    char object_name[512];
    const char *object_file;
    if (argc != 3 && argc != 4){
        printf("Usage: %s program.asm Instruction_Byte_Diagram.txt [output.o]\n", argv[0]);
        return 1;
    }
    if (argc == 4) object_file = argv[3];
    else {
        snprintf(object_name, sizeof(object_name), "%s", argv[1]);
        { char *dot = strrchr(object_name, '.'); if (dot) *dot = '\0'; }
        strncat(object_name, ".o", sizeof(object_name) - strlen(object_name) - 1);
        object_file = object_name;
    }
    load_instruction_set(argv[2]);
    first_pass(argv[1]);
    process_file(argv[1], object_file);
    printf("\nRaw object bytes written to: %s\n", object_file);
    return 0;
}

