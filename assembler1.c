#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
} Operand;

typedef struct {
    unsigned char bytes[MAX_BYTES];
    int count;
} Code;

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
    lower(t);
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
    init_operand(o);
    strcpy(t,s);
    trim(t);
    lower(t);
    if (!strncmp(t,"dword",5)) {
        memmove(t,t+5,strlen(t+5)+1);
        trim(t);
    }
    else if (!strncmp(t,"word",4)) {
        memmove(t,t+4,strlen(t+4)+1);
        trim(t);
    }
    else if (!strncmp(t,"byte",4)) {
        memmove(t,t+4,strlen(t+4)+1);
        trim(t);
    }
    strcpy(o->text,t);
    if (strchr(t,'[')) {
        if (parse_memory(t,o))
            return;
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
    char rest[MAX_LINE];
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
unsigned long immediate_value(Operand *o) {
    if(!strcmp(o->kind,"IMM"))
        return get_number(o->text);
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

int is_data_directive(const char *s) {
    return !strcmp(s,"db") || !strcmp(s,"dw") || !strcmp(s,"dd") || !strcmp(s,"resb") || !strcmp(s,"resw") || !strcmp(s,"resd");
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
void process_file(const char *file){
    FILE *fp;
    char line[MAX_LINE];
    char section[32]="";
    int section_address=0;
    fp = fopen(file,"r");
    if (!fp){
        perror(file);
        exit(1);
    }
    printf("\nADDRESS    HEX BYTES                         SOURCE\n");
    printf("---------------------------------------------------------------\n");
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
            printf("--         --                                %s",original);
            continue;
        }
        if (!strcmp(section,".data") || !strcmp(section,".bss")){
            Code c={0};
            unsigned int address=calculate_address(0,section_address,1);
            if (process_data(line,&c)){
                if (c.count){
                    printf("%08X  ",address);
                    print_code(&c,address);
                    printf("    %s",original);
                    section_address += c.count;
                }
                else{
                    int size=reserved_size(line);
                    printf("%08X  --                                %s",address,original);
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
                    printf("%08X  --                                %s",address,original);
                    continue;
                }
            }
            if (!parse_instruction(line,mn,atext,btext))
                continue;
            parse_operand(atext,&a);
            if (btext[0])
                parse_operand(btext,&b);
            else
                init_operand(&b);
            op = find_instruction(mn,&a,&b,btext[0] != '\0');
            printf("%08X  ",address);
            if(op){
                encode(op,&a,&b,&c);
                print_code(&c,address);
                section_address += c.count;
            }
            else {
                printf("??");
            }
            printf("    %s",original);
        }
    }
    fclose(fp);
}
int main(int argc,char *argv[]){
    if (argc != 3){
        printf(
            "Usage: %s program.asm Instruction_Byte_Diagram.txt\n",
            argv[0]
        );
        return 1;
    }
    load_instruction_set(argv[2]);
    process_file(argv[1]);
    return 0;
}

