#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <errno.h>
#include <pwd.h>
#include <fcntl.h>
#include "build_in_command.h"

//���뻺�����������
#define BUF_SIZE 1024

//����������
#define PARAMETER_SIZE 32

//fd��������
#define MAX_FD 1024

//�ض���
#define REDIR_NONE 0B0001 //û���ض���
#define REDIR_INPUT 0B0010 //�����ض���
#define REDIR_OUTPUT 0B0100 //����ض���
#define REDIR_APPEND 0B1000 //׷���ض���

// ���õ�״̬��
enum {
    RESULT_NORMAL,//����
    ERROR_FORK, //fork�쳣
    ERROR_COMMAND, //�������
    ERROR_WRONG_PARAMETER, //����Ĳ���
    ERROR_MISS_PARAMETER, //ȱʧ����
    ERROR_TOO_MANY_PARAMETER, //�������
    ERROR_CD, //cdʧ��
    ERROR_SYSTEM, //ϵͳ����
    ERROR_MEMORY_ALLOCATION, //�ڴ�������
    ERROR_EXIT, //�˳�����
    ERROR_INPUT_EXCEED, //�ն����볬��
    ERROR_READING_INPUT, //���������

    /* �ض���Ĵ�����Ϣ */
    ERROR_MANY_IN, //
    ERROR_MANY_OUT, //�ض����ļ�����
    ERROR_FILE_NOT_EXIST, //�ض����ļ�������

    /* �ܵ��Ĵ�����Ϣ */
    ERROR_PIPE,
    ERROR_PIPE_MISS_PARAMETER //�ܵ�ȱʧ����
};

//�ܵ�

//ԭ������ṹ������
typedef struct
{
    /* data */
    char* command;//������
    char** argv;//�������
    char* redir_in_filename;//�ض��������ļ���
    char* redir_out_filename;//�ض�������ļ���
    int redir_type;//�ض�������
}atomic_command;

/// @brief ����ȫ�ֱ���
char input[BUF_SIZE];//�洢�û�����
char* background_commands[BUF_SIZE];//�洢��̨����(�������� & )
char* atomCmdString[BUF_SIZE / 4];//���ڴ洢ԭ������
atomic_command* atomCmd[BUF_SIZE / 4];//ԭ������ṹ��
size_t nread = 0;//���ն��ж�ȡ���ַ�����

//pipe fd
int* pipe_fd;

//�ִʷ���
const char* SEP = " ";//����ָ���
const char* PAR = "&";//��̨����
const char* PIPE = "|";//�ܵ�
const char* REDIR_IN_CHAR = "<";//�����ض���
const char* REDIR_OUT_CHAR = ">";//����ض���
const char* REDIR_APP_CHAR = ">>";//׷���ض���


/// @brief ��ʾ��
char username[BUF_SIZE];//�û���
char hostname[BUF_SIZE];//������
char curPath[BUF_SIZE];//��ǰ����Ŀ¼
char prompt[BUF_SIZE];//��ʾ��

/// @brief ��ʼ���͸�����ʾ��
void initGlobal();
void initPrompt();
void init_atomic_command(atomic_command* cmd);
int malloc_pipe_fd(int num);
void free_pipe_fd();
void updatePrompt();

/// @brief ��ȡ�û����뺯������
int getUserInput();//��ȡ����

/// @brief ����ִʺ�������
void splitUserInputIntoBGcmd();//���û����������ͨ��&�ִ�Ϊ��̨����
int splitAtomicCmd(char* cmdString, int index);//�ִʵ���ԭ��ָ��,����ض���
void splitBGcmdAndRun(char* bgcommand);//����ÿһ����̨�������, ��̨��������ܵ����ض���

/// @brief ������
int checkRedirection(atomic_command* cmd);//���ԭ�������Ƿ����ض���

/// @brief ִ������
int callCommand(int num_cmd);//��������

/// @brief ������ʾ
void reportError(int ERROR_TYPE);

/// @brief DEBUG,��ӡԭ��ָ��ṹ�庯��
void printAtomicCommand(atomic_command* cmd);

//shell��������
#define NOT_BUILD_IN 0
#define CD_COMMAND 1
#define HELP_COMMAND 2
#define EXPORT_COMMAND 3
#define EXIT_COMMAND 4
#define PWD_COMMAND 5
int check_build_in_command(char* command);
int cd_command(char* path);
int help_command();
int export_command(char* var);
int exit_command();
int pwd_command();
int call_build_in_command(int cmdtype,  char* args);

int main()
{
    int res = RESULT_NORMAL;

    //��ʼ����ʾ��
    initPrompt();

    //ѭ����ȡ�û�����
    while (1)
    {
        printf("%s", prompt);//�����ʾ��

        //��ȡ�û�����,���ҵ��û��������ʱ��ʾ�û�
        if ((res = getUserInput()) != RESULT_NORMAL)
        {
            reportError(res);
            continue;
        }

        //�ָ����Ϊ�����̨����
        splitUserInputIntoBGcmd();

        int idx = 0;
        while (background_commands[idx] != NULL)
        {
            //���������к�̨����
            splitBGcmdAndRun(background_commands[idx]);
            idx++;
        }
    }
    return 0;
}

void initGlobal()
{
    pipe_fd = NULL;

    for (int i = 0; i < BUF_SIZE; ++i)
        background_commands[i] = NULL;

    for (int i = 0; i < BUF_SIZE / 4; ++i)
    {
        atomCmdString[i] = NULL;
        atomCmd[i] = NULL;
    }
}

static void getUsername()
{
    struct passwd* pwd = getpwuid(getuid());
    strcpy(username, pwd->pw_name);
}

static void getHostname()
{
    gethostname(hostname, BUF_SIZE);
}

int getCurWorkDir() { // ��ȡ��ǰ�Ĺ���Ŀ¼
    //��ȡĿ¼ʧ��ʱ,����NULL
    char* result = getcwd(curPath, BUF_SIZE);
    if (result == NULL)
        return ERROR_SYSTEM;
    return RESULT_NORMAL;
}

void initPrompt()
{
    /* ��ȡ��ǰ����Ŀ¼���û����������� */
    int result = getCurWorkDir();
    if (ERROR_SYSTEM == result) {
        fprintf(stderr, "\e[31;1mError: System error while getting current work directory.\n\e[0m");
        exit(ERROR_SYSTEM);
    }
    getUsername();

    getHostname();

    // ��ʽ����ʾ�����洢�� prompt ��
    snprintf(prompt, BUF_SIZE, "\e[32;1m%s@%s:%s\e[0m$ ", username, hostname, curPath);
}

// ��ʼ������
void init_atomic_command(atomic_command* cmd) {
    if (cmd == NULL) {
        return;
    }

    cmd->command = NULL;              // ��ʼ��������Ϊ��
    cmd->argv = NULL;                 // ��ʼ�������б�Ϊ��
    cmd->redir_in_filename = NULL;    // ��ʼ�������ض����ļ���Ϊ��
    cmd->redir_out_filename = NULL;   // ��ʼ������ض����ļ���Ϊ��
    cmd->redir_type = 0;              // ��ʼ���ض�������Ϊ 0
}

//����ܵ��ռ�
int malloc_pipe_fd(int num)
{
    if (num == 0)
        return RESULT_NORMAL;

    pipe_fd = (int*)malloc(num * sizeof(int));
    if (!pipe_fd)
        return ERROR_MEMORY_ALLOCATION;

    return RESULT_NORMAL;
}

//�ͷŹܵ��ռ�
void free_pipe_fd()
{
    if (pipe_fd)
        free(pipe_fd);
}

void updatePrompt()
{
    initPrompt();
}

//������뻺����
static void clear_stdin() {
    // �����׼���뻺�����е�ʣ������
    char ch;
    while ((ch = getchar()) != '\n' && ch != EOF); // ��ȡֱ���������з�
}

/// @brief 
/// ʹ��fgets��ȡ�û�����,�������
/// 1.�����ַ����� < BUF_SIZE - 1, ��β����'\n\0'
/// 2.�����ַ����� = BUF_SIZE - 1, ��βֻ��'\0'
/// 3.�����ַ����� > BUF_SIZE, �ض�[0,BUF_SIZE-1],��BUF_SIZEλ�ü�'\0',ʣ��������ڻ�����,��Ҫ������
/// @return ��ȡ�����״̬--1.���� 2.�������� 3.��ȡʧ��
int getUserInput()
{
    nread = 0;//����nread

    //��ȡ�û�����
    if (fgets(input, sizeof(input), stdin) == NULL)
        return ERROR_READING_INPUT;

    //ʹ��fgets�Ὣ\n��������
    nread = strlen(input);
    // ����Ƿ������볬����������С
    // fgets�����ض�ʱֻ��ȡBUF_SIZE-1���ַ�,������'\0',���ҽ������Ĳ��������ڻ�������
    if (nread == BUF_SIZE - 1 && input[BUF_SIZE - 2] != '\n') {
        printf("Input too long, please try again.\n");
        // �����׼�����е�ʣ������
        clear_stdin();
        return ERROR_INPUT_EXCEED;
    }

    if (nread == BUF_SIZE - 1 && input[BUF_SIZE - 2] == '\n')
        input[BUF_SIZE - 2] = '\0';
    else
        input[nread - 1] = '\0';
    return RESULT_NORMAL;
}

//���û����������ͨ��&�ִ�Ϊ��̨����
void splitUserInputIntoBGcmd()
{
    char* token = NULL;
    int index = 0;

    //���ȷָ��̨���� & , ����̨����洢��background_commands��
    token = strtok(input, PAR);
    while (token != NULL)
    {
        //��ӡ
        //printf("%s\n", token);
        background_commands[index++] = token;
        token = strtok(NULL, PAR);
    }
    background_commands[index] = NULL;//���һ����Ч�������һ��ΪNULL������ֹ�ж�
}

int splitAtomicCmd(char* cmdString, int index)
{
    char* in_token;
    int in_index = 0;

    // ����Ƿ��Ѿ�Ϊ atomCmd[index] �������ڴ棬����Ѿ����䣬���ͷ�֮ǰ���ڴ�
    if (atomCmd[index] != NULL) {
        // ����ѷ����ڴ棬���ͷ�
        free(atomCmd[index]->argv);  // �ͷ� argv
        free(atomCmd[index]);         // �ͷ� atomic_command �ṹ��
    }
    atomCmd[index] = (atomic_command*)malloc(sizeof(atomic_command));
    init_atomic_command(atomCmd[index]);
    if (!atomCmd[index]) {
        perror("malloc failed for atomic_command");
        return ERROR_MEMORY_ALLOCATION; // ȷ�������ڴ����ʧ��
    }
    atomCmd[index]->argv = (char**)malloc(PARAMETER_SIZE * sizeof(char*));
    if (!atomCmd[index]->argv) {
        perror("malloc failed for argv");
        free(atomCmd[index]);  // �ͷ�֮ǰΪ atomCmd[index] ������ڴ�
        return ERROR_MEMORY_ALLOCATION;
    }
    for (int i = 0; i < PARAMETER_SIZE; i++)
        atomCmd[index]->argv[i] = NULL;
    in_token = strtok(cmdString, SEP);//��һ�����ֳ�����������
    atomCmd[index]->command = in_token;
    atomCmd[index]->argv[in_index++] = in_token;
    in_token = strtok(NULL, SEP);//�����ִ�
    while (in_token != NULL && in_index < PARAMETER_SIZE - 1) {
        atomCmd[index]->argv[in_index++] = in_token;
        //printf("%s ",in_token);
        in_token = strtok(NULL, SEP);
    }
    //printf("\n\n");

    //��������
    if (in_index == PARAMETER_SIZE - 1 && in_token != NULL)
        return ERROR_TOO_MANY_PARAMETER;
    atomCmd[index]->argv[in_index] = NULL;

    //����Ƿ����ض���
    int res = checkRedirection(atomCmd[index]);
    if (res != RESULT_NORMAL)
        return res;

    return RESULT_NORMAL;
}

//������̨����Ϊԭ������,�Լ�ԭ�������Ĺ�ϵ,������
void splitBGcmdAndRun(char* bgcommand)
{
    char* token = NULL;
    int index = 0;
    int command_type = 0;
    int splitres = 0;
    //ʹ�� | �ָ��̨����Ϊԭ������
    token = strtok(bgcommand, PIPE);
    while (token != NULL)
    {
        atomCmdString[index++] = token;
        token = strtok(NULL, PIPE);
    }
    atomCmdString[index] = NULL;

    //��ԭ�������ַ���ת����ԭ������ṹ��
    for (int i = 0; i < index; ++i)
    {
        splitres = splitAtomicCmd(atomCmdString[i], i);
        if (splitres != RESULT_NORMAL)
        {
            reportError(splitres);
            return;
        }

        //����Ƿ�����������
        command_type = check_build_in_command(atomCmd[i]->command);
        if (command_type != 0)
        {
            call_build_in_command(command_type, atomCmd[i]->argv[1]);
            return;
        }
    }
    
    //ִ��ÿһ��ԭ��ָ��,�ض������ȼ����ڹܵ�
    callCommand(index);
}


//���ԭ��������ض���
int checkRedirection(atomic_command* cmd)
{
    if (cmd == NULL)
        return ERROR_SYSTEM;

    int index = 0;
    int redir_in_count = 0;
    int redir_out_count = 0;
    int redir_app_count = 0;

    // ���������б�
    while (cmd->argv[index] != NULL)
    {
        // ��������ض���
        if (strcmp(cmd->argv[index], REDIR_IN_CHAR) == 0) {
            // ֻ�������ض�����ţ�û���ļ���
            if (cmd->argv[index + 1] == NULL) {
                return ERROR_MISS_PARAMETER;  // ȱʧ�����ļ���
            }

            if (redir_in_count > 0) {
                return ERROR_MANY_IN;  // �����ض�����ų��ֶ��
            }

            // ���������ض����ļ���
            cmd->redir_in_filename = cmd->argv[index + 1];
            cmd->redir_type |= REDIR_INPUT;
            redir_in_count++;
            // ���ض�����ź��ļ����Ӳ����б����Ƴ���������������ǰ��
            cmd->argv[index] = NULL;
            cmd->argv[index + 1] = NULL;

            // �ƶ���������
            int i = index + 2;
            while (cmd->argv[i] != NULL) {
                cmd->argv[i - 2] = cmd->argv[i];
                cmd->argv[i] = NULL;
                i++;
            }
            index += 2;  // ���������ض�����ź��ļ���
        }
        // �������ض���
        else if (strcmp(cmd->argv[index], REDIR_OUT_CHAR) == 0) {
            // ֻ������ض�����ţ�û���ļ���
            if (cmd->argv[index + 1] == NULL) {
                return ERROR_MISS_PARAMETER;  // ȱʧ����ļ���
            }

            if (redir_out_count > 0) {
                return ERROR_MANY_OUT;  // ����ض�����ų��ֶ��
            }

            // ��������ض����ļ���
            cmd->redir_out_filename = cmd->argv[index + 1];
            cmd->redir_type |= REDIR_OUTPUT;
            redir_out_count++;

            // ���ض�����ź��ļ����Ӳ����б����Ƴ���������������ǰ��
            cmd->argv[index] = NULL;
            cmd->argv[index + 1] = NULL;

            // �ƶ���������
            int i = index + 2;
            while (cmd->argv[i] != NULL) {
                cmd->argv[i - 2] = cmd->argv[i];
                cmd->argv[i] = NULL;
                i++;
            }
            index += 2;  // ��������ض�����ź��ļ���
        }
        // ���׷������ض���
        else if (strcmp(cmd->argv[index], REDIR_APP_CHAR) == 0) {
            // ֻ��׷���ض�����ţ�û���ļ���
            if (cmd->argv[index + 1] == NULL) {
                return ERROR_MISS_PARAMETER;  // ȱʧ׷���ļ���
            }

            if (redir_app_count > 0) {
                return ERROR_MANY_OUT;  // ׷���ض�����ų��ֶ��
            }

            // ����׷������ض����ļ���
            cmd->redir_out_filename = cmd->argv[index + 1];
            cmd->redir_type |= REDIR_APPEND;
            redir_app_count++;
            // ���ض�����ź��ļ����Ӳ����б����Ƴ���������������ǰ��
            cmd->argv[index] = NULL;
            cmd->argv[index + 1] = NULL;

            // �ƶ���������
            int i = index + 2;
            while (cmd->argv[i] != NULL) {
                cmd->argv[i - 2] = cmd->argv[i];
                cmd->argv[i] = NULL;
                i++;
            }
            index += 2;  // ����׷���ض�����ź��ļ���
        }
        else {
            // ����������������
            index++;
        }
    }

    // ��������ļ��Ƿ����
    if (cmd->redir_in_filename != NULL) {
        if (access(cmd->redir_in_filename, F_OK) == -1) {
            return ERROR_FILE_NOT_EXIST;  // �����ļ�������
        }
    }

    // �������ļ��Ƿ����
    if (cmd->redir_out_filename != NULL) {
        if (access(cmd->redir_out_filename, F_OK) == -1) {
            int fd = open(cmd->redir_out_filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (fd == -1) {
                perror("Failed to open or create file");
                return ERROR_FILE_NOT_EXIST;  // �ļ��޷��򿪻򴴽�
            }

            close(fd);
            printf("create file %s", cmd->redir_out_filename);
        }
    }

    return RESULT_NORMAL;  // һ������
}


int callCommand(int num_cmd)
{
    int i;
    int pipe_num = num_cmd - 1;
    malloc_pipe_fd(2 * pipe_num);

    // �����ܵ�
    for (i = 0; i < pipe_num; ++i)
    {
        if (pipe(pipe_fd + i * 2) == -1) {
            perror("pipe failed");
            exit(1);
        }
    }

    // ִ��ÿ��ԭ������
    for (i = 0; i < num_cmd; ++i)
    {
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork failed");
            exit(1);
        }

        if (pid == 0) {  // �ӽ���
            // ���������ض���
            if (atomCmd[i]->redir_in_filename != NULL) {
                int in_fd = open(atomCmd[i]->redir_in_filename, O_RDONLY);
                if (in_fd == -1) {
                    perror("Input redirection failed");
                    exit(1);
                }
                if (dup2(in_fd, STDIN_FILENO) == -1) {
                    perror("dup2 stdin failed");
                    exit(1);
                }
                close(in_fd);
            }

            // ��������ض���
            if (atomCmd[i]->redir_out_filename != NULL) {
                int out_fd;
                if ((atomCmd[i]->redir_type & REDIR_OUTPUT) != 0) {  // ���� `>`
                    out_fd = open(atomCmd[i]->redir_out_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                }
                else if ((atomCmd[i]->redir_type & REDIR_APPEND) != 0) {  // ���� `>>`
                    out_fd = open(atomCmd[i]->redir_out_filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
                }
                if (out_fd == -1) {
                    perror("Output redirection failed");
                    exit(1);
                }
                if (dup2(out_fd, STDOUT_FILENO) == -1) {
                    perror("dup2 stdout failed");
                    exit(1);
                }
                close(out_fd);
            }

            //û�������ض���
            if ((atomCmd[i]->redir_type & REDIR_INPUT) == 0)
            {
                // ������ǵ�һ������ض�������Ϊǰһ���ܵ��Ķ���
                if (i > 0) {
                    if (dup2(pipe_fd[(i - 1) * 2], STDIN_FILENO) == -1) {
                        perror("dup2 stdin failed");
                        exit(1);
                    }
                }
            }

            //û������ض����׷�ӴӶ���
            if ((atomCmd[i]->redir_type & REDIR_OUTPUT) == 0 && (atomCmd[i]->redir_type & REDIR_APPEND) == 0)
            {
                // ����������һ������ض������Ϊ��ǰ�ܵ���д��
                if (i < num_cmd - 1) {
                    if (dup2(pipe_fd[i * 2 + 1], STDOUT_FILENO) == -1) {
                        perror("dup2 stdout failed");
                        exit(1);
                    }
                }
            }

            // �ر����йܵ��ļ�������
            for (int j = 0; j < 2 * (num_cmd - 1); ++j) {
                close(pipe_fd[j]);
            }

            // ִ������
            //printAtomicCommand(atomCmd[i]);
            execvp(atomCmd[i]->command, atomCmd[i]->argv);
            perror("execvp failed");
            exit(1);  // ��� execvp ʧ�ܣ��˳��ӽ���
        }
    }

    // �����̹ر����йܵ��ļ�������
    for (int i = 0; i < 2 * (num_cmd - 1); ++i) {
        close(pipe_fd[i]);
    }

    // �����̵ȴ������ӽ���
    for (int i = 0; i < num_cmd; ++i) {
        wait(NULL);
    }
}

//DEBUG
//��ӡatomic_command�ṹ������
void printAtomicCommand(atomic_command* cmd)
{
    if (cmd == NULL) {
        printf("Error: atomic_command is NULL\n");
        return;
    }

    // ��ӡ��������ȷ������NULL���
    if (cmd->command == NULL) {
        printf("Command: NULL\n");
    }
    else {
        printf("Command: %s\n", cmd->command);
    }

    // ��ӡ�������
    printf("Arguments: ");
    if (cmd->argv != NULL) {
        int i = 0;
        while (cmd->argv[i] != NULL) {
            printf("'%s' ", cmd->argv[i]);
            i++;
        }
    }
    printf("\n");

    // ��ӡ�����ض����ļ���
    if (cmd->redir_in_filename != NULL) {
        printf("Input redirection: %s\n", cmd->redir_in_filename);
    }

    // ��ӡ����ض����ļ���
    if (cmd->redir_out_filename != NULL) {
        printf("Output redirection: %s\n", cmd->redir_out_filename);
    }

    // ��ӡ�ض�������
    printf("Redirection type: ");
    if (cmd->redir_type & REDIR_INPUT) {
        printf("Input ");
    }
    if (cmd->redir_type & REDIR_OUTPUT) {
        printf("Output ");
    }
    if (cmd->redir_type & REDIR_APPEND) {
        printf("Append ");
    }
    if (cmd->redir_type == REDIR_NONE) {
        printf("None ");
    }
    printf("\n");
}

// ����Ƿ�����������
int check_build_in_command(char* command) {
    if (strcmp(command, "cd") == 0) {
        return CD_COMMAND; // cd ����
    }
    else if (strcmp(command, "help") == 0) {
        return HELP_COMMAND; // help ����
    }
    else if (strcmp(command, "export") == 0) {
        return EXPORT_COMMAND; // export ����
    }
    else if (strcmp(command, "exit") == 0) {
        return EXIT_COMMAND; // exit ����
    }
    else if (strcmp(command, "pwd") == 0) {
        return PWD_COMMAND; // pwd ����
    }
    else {
        return 0; // ������������
    }// cd ����л�����Ŀ¼
}


int cd_command(char* path) {
    if (path == NULL || strcmp(path, "") == 0) {
        path = getenv("HOME"); // Ĭ���л��� HOME Ŀ¼
        if (path == NULL) {
            fprintf(stderr, "cd: No HOME directory set\n");
            return -1;
        }
    }

    // ���Ը���Ŀ¼
    if (chdir(path) == -1) {
        perror("cd failed");
        return -1;
    } else {
        char cwd[BUF_SIZE];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("Changed directory to: %s\n", cwd);
        }
    }

    updatePrompt();

    return 0;
}

// help �����ʾ������Ϣ
int help_command() {
    printf("Available commands:\n");
    printf("cd <dir>      : Change directory to <dir>\n");
    printf("help          : Show this help message\n");
    printf("export <var>  : Set environment variable\n");
    printf("exit          : Exit the shell\n");
    printf("pwd           : Show current working directory\n");
    return 0;
}

// export ������û�������
int export_command(char* var) {
    char* value = strchr(var, '='); // ���ҵȺ�
    if (value != NULL) {
        *value = '\0'; // ���Ⱥ��滻�� '\0'���ָ�������ͱ���ֵ
        value++; // �ƶ����Ⱥź����ֵ
        if (setenv(var, value, 1) == -1) {
            perror("export failed");
            return -1;
        }
        printf("Exported: %s=%s\n", var, value);
    } else {
        fprintf(stderr, "export: invalid format\n");
        return -1;
    }
    return 0;
}

// exit ����˳� shell
int exit_command() {
    printf("Exiting shell...\n");
    exit(0); // �˳� shell
}

// pwd �����ʾ��ǰ����Ŀ¼
int pwd_command() {
    char cwd[BUF_SIZE];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("Current directory: %s\n", cwd);
    } else {
        perror("pwd failed");
        return -1;
    }
    return 0;
}

// �������ִ��
int call_build_in_command(int cmdtype, char* args)
{
    switch (cmdtype) {
        case CD_COMMAND: // cd ����
            return cd_command(args);
        case HELP_COMMAND: // help ����
            return help_command();
        case EXPORT_COMMAND: // export ����
            return export_command(args);
        case EXIT_COMMAND: // exit ����
            return exit_command();
        case PWD_COMMAND: // pwd ����
            return pwd_command();
        default:
            fprintf(stderr, "Command not found!\n");
            return -1;
    }
}

// ���󱨸溯��
void reportError(int ERROR_TYPE) {
    switch (ERROR_TYPE) {
    case RESULT_NORMAL:
        // �������������Ҫ���
        break;

    case ERROR_FORK:
        perror("Fork failed");
        break;

    case ERROR_COMMAND:
        fprintf(stderr, "Error: Command not found.\n");
        break;

    case ERROR_WRONG_PARAMETER:
        fprintf(stderr, "Error: Incorrect parameters.\n");
        break;

    case ERROR_MISS_PARAMETER:
        fprintf(stderr, "Error: Missing parameters.\n");
        break;

    case ERROR_TOO_MANY_PARAMETER:
        fprintf(stderr, "Error: Too many parameters.\n");
        break;

    case ERROR_CD:
        perror("cd failed");
        break;

    case ERROR_SYSTEM:
        perror("System error");
        break;

    case ERROR_MEMORY_ALLOCATION:
        fprintf(stderr, "Error: Memory allocation failed.\n");
        break;

    case ERROR_EXIT:
        fprintf(stderr, "Error: Exit failed.\n");
        break;

    case ERROR_INPUT_EXCEED:
        fprintf(stderr, "Error: Input exceeds allowed limit.\n");
        break;

    case ERROR_READING_INPUT:
        perror("Reading input failed");
        break;

        // �ض���Ĵ���
    case ERROR_MANY_IN:
        fprintf(stderr, "Error: Too many input redirection files.\n");
        break;

    case ERROR_MANY_OUT:
        fprintf(stderr, "Error: Too many output redirection files.\n");
        break;

    case ERROR_FILE_NOT_EXIST:
        fprintf(stderr, "Error: Redirected file does not exist.\n");
        break;

        // �ܵ��Ĵ���
    case ERROR_PIPE:
        fprintf(stderr, "Error: Pipe creation failed.\n");
        break;

    case ERROR_PIPE_MISS_PARAMETER:
        fprintf(stderr, "Error: Missing parameters for pipe.\n");
        break;

    default:
        fprintf(stderr, "Unknown error occurred.\n");
        break;
    }
}