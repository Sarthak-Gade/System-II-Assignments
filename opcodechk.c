#include <stdio.h>
#include <string.h>
#include <ctype.h>
#define MAX 200
#define MAX_MNEMONICS 100
void ltrim(char str[])
{
    int i = 0;
    int j = 0;
    while (str[i] == ' ' || str[i] == '\t')
        i++;
    while (str[i] != '\0')
    {
        str[j] = str[i];
        i++;
        j++;
    }
    str[j] = '\0';
}
void toLowerCase(char str[])
{
    int i;
    for (i = 0; str[i] != '\0'; i++)
        str[i] = tolower((unsigned char)str[i]);
}
void trim(char str[])
{
    int length;
    ltrim(str);
    length = strlen(str);
    while (length > 0 &&
           (str[length - 1] == ' ' ||
            str[length - 1] == '\t' ||
            str[length - 1] == '\n' ||
            str[length - 1] == '\r'))
    {
        str[length - 1] = '\0';
        length--;
    }
}
int isRegister(char operand[])
{
    char *registers[] = {
        "eax", "ebx", "ecx", "edx",
        "esi", "edi", "esp", "ebp",
        "ax", "bx", "cx", "dx",
        "al", "ah", "bl", "bh",
        "cl", "ch", "dl", "dh"
    };
    int n = sizeof(registers) / sizeof(registers[0]);
    int i;
    for (i = 0; i < n; i++)
    {
        if (strcmp(operand, registers[i]) == 0)
            return 1;
    }
    return 0;
}
int isConstant(char operand[])
{
    int i = 0;
    if (operand[0] == '0' &&
        (operand[1] == 'x' || operand[1] == 'X'))
    {
        i = 2;
        if (operand[i] == '\0')
            return 0;
        while (operand[i] != '\0')
        {
            if (!isxdigit((unsigned char)operand[i]))
                return 0;
            i++;
        }
        return 1;
    }
    if (operand[0] == '+' || operand[0] == '-')
        i = 1;
    if (operand[i] == '\0')
        return 0;
    while (operand[i] != '\0')
    {
        if (!isdigit((unsigned char)operand[i]))
            return 0;
        i++;
    }
    return 1;
}
void getOperandType(char operand[], char type[])
{
    trim(operand);
    toLowerCase(operand);
    if (strchr(operand, '[') != NULL &&
        strchr(operand, ']') != NULL)
    {
        strcpy(type, "MEMORY");
    }
    else if (isRegister(operand))
    {
        strcpy(type, "REGISTER");
    }
    else if (isConstant(operand))
    {
        strcpy(type, "CONSTANT");
    }
    else
    {
        strcpy(type, "SYMBOL");
    }
}
void addMnemonic(char mnemonics[][MAX], int *count, char word[])
{
    int i;
    trim(word);
    toLowerCase(word);
    if (word[0] == '\0')
        return;
    for (i = 0; i < *count; i++)
    {
        if (strcmp(mnemonics[i], word) == 0)
            return;
    }
    if (*count >= MAX_MNEMONICS)
        return;
    strcpy(mnemonics[*count], word);
    (*count)++;
}
int isInOpcodeTable(char mnemonics[][MAX], int count, char word[])
{
    int i;
    for (i = 0; i < count; i++)
    {
        if (strcmp(mnemonics[i], word) == 0)
            return 1;
    }
    return 0;
}
int main(int argc, char *argv[])
{
    FILE *opcodeFile;
    FILE *assemblyFile;
    char line[MAX];
    char word[MAX];
    char mnemonics[MAX_MNEMONICS][MAX];
    int mnemonicCount = 0;
    char mnemonic[MAX];
    char operands[MAX];
    char operand1[MAX];
    char operand2[MAX];
    char type1[20];
    char type2[20];
    char *p;
    char *comma;
    int inTextSection = 0;
    if (argc != 3)
    {
        printf("Usage: %s <opcode_file> <assembly_file>\n", argv[0]);
        return 1;
    }
    opcodeFile = fopen(argv[1], "r");
    if (opcodeFile == NULL)
    {
        printf("Cannot open opcode file: %s\n", argv[1]);
        return 1;
    }
    while (fgets(line, sizeof(line), opcodeFile) != NULL)
    {
        trim(line);
        if (line[0] == '\0')
            continue;
        if (line[0] == ';')
            continue;
        if (sscanf(line, "%s", word) == 1)
        {
            if (strcmp(word, "Instruction") == 0 ||
                strcmp(word, "Name") == 0 ||
                strcmp(word, "Opcode") == 0 ||
                strcmp(word, "InstructionName") == 0)
            {
                continue;
            }

            addMnemonic(mnemonics, &mnemonicCount, word);
        }
    }

    fclose(opcodeFile);
    assemblyFile = fopen(argv[2], "r");
    if (assemblyFile == NULL)
    {
        printf("Cannot open assembly file: %s\n", argv[2]);
        return 1;
    }
    printf("Mnemonic\tOperand\t\tType\n");
    printf("-----------------------------------------------\n");
    while (fgets(line, sizeof(line), assemblyFile) != NULL)
    {
        trim(line);
        if (line[0] == '\0')
            continue;
        if (line[0] == ';')
            continue;
        if (strcmp(line, "section .text") == 0)
        {
            inTextSection = 1;
            continue;
        }
        if (strcmp(line, "section .data") == 0)
        {
            inTextSection = 0;
            continue;
        }
        if (strcmp(line, "section .bss") == 0)
        {
            inTextSection = 0;
            continue;
        }
        if (!inTextSection)
            continue;
        if (strncmp(line, "global ", 7) == 0 ||
            strncmp(line, "extern ", 7) == 0)
        {
            continue;
        }

        if (strchr(line, ':') != NULL)
            continue;
        if (sscanf(line, "%s", mnemonic) != 1)
            continue;
        toLowerCase(mnemonic);
        p = line;
        while (*p != ' ' &&
               *p != '\t' &&
               *p != '\0')
        {
            p++;
        }
        if (*p == '\0')
        {
            printf("%s\t\t-\t\t-\n", mnemonic);
            continue;
        }
        while (*p == ' ' || *p == '\t')
            p++;
        strcpy(operands, p);
        p = strchr(operands, ';');
        if (p != NULL)
            *p = '\0';
        trim(operands);
        if (operands[0] == '\0')
        {
            printf("%s\t\t-\t\t-\n", mnemonic);
            continue;
        }
        comma = strchr(operands, ',');
        if (comma != NULL)
        {
            int length;
            length = comma - operands;
            if (length >= MAX)
                length = MAX - 1;
            strncpy(operand1, operands, length);
            operand1[length] = '\0';
            getOperandType(operand1, type1);
            strncpy(operand2, comma + 1, MAX - 1);
            operand2[MAX - 1] = '\0';
            getOperandType(operand2, type2);
            printf("%s\t\t%s\t\t%s\n",mnemonic, operand1, type1);
            printf("\t\t%s\t\t%s\n",operand2, type2);
        }
        else
        {
            strcpy(operand1, operands);
            getOperandType(operand1, type1);
            printf("%s\t\t%s\t\t%s\n",mnemonic, operand1, type1);
        }
    }
    fclose(assemblyFile);
    return 0;
}
