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
const char* REDIR_IN = "<";//�����ض���
const char* REDIR_OUT = ">";//����ض���
const char* REDIR_APP = ">>";//׷���ض���


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
void checkCommandvalidity();//����������Ч�ԣ��Ƿ���Ҫ��
int checkRedirection(atomic_command* cmd);//���ԭ�������Ƿ����ض���

/// @brief ִ������
int callCommand(int num_cmd);//��������

/// @brief ������ʾ
void reportError(int ERROR_TYPE);

/// @brief DEBUG,��ӡԭ��ָ��ṹ�庯��
void printAtomicCommand(atomic_command* cmd);

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
        splitAtomicCmd(atomCmdString[i], i);
        //printAtomicCommand(atomCmd[i]);
        //printf("\n\n");
    }

    //ִ��ÿһ��ԭ��ָ��,�ض������ȼ����ڹܵ�
    callCommand(index);
}

void checkCommandvalidity()
{

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
        if (strcmp(cmd->argv[index], REDIR_IN) == 0) {
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
            index += 2;  // ���������ض�����ź��ļ���
        }
        // �������ض���
        else if (strcmp(cmd->argv[index], REDIR_OUT) == 0) {
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
            index += 2;  // ��������ض�����ź��ļ���
        }
        // ���׷������ض���
        else if (strcmp(cmd->argv[index], REDIR_APP) == 0) {
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
    if (cmd->redir_out_filename != NULL || cmd->redir_out_filename != NULL) {
        if (access(cmd->redir_out_filename, F_OK) == -1) {
            return ERROR_FILE_NOT_EXIST;  // ����ļ�������
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
                if (atomCmd[i]->redir_type == 1) {  // ���� `>`
                    out_fd = open(atomCmd[i]->redir_out_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                }
                else if (atomCmd[i]->redir_type == 2) {  // ���� `>>`
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

            // ������ǵ�һ������ض�������Ϊǰһ���ܵ��Ķ���
            if (i > 0) {
                if (dup2(pipe_fd[(i - 1) * 2], STDIN_FILENO) == -1) {
                    perror("dup2 stdin failed");
                    exit(1);
                }
            }

            // ����������һ������ض������Ϊ��ǰ�ܵ���д��
            if (i < num_cmd - 1) {
                if (dup2(pipe_fd[i * 2 + 1], STDOUT_FILENO) == -1) {
                    perror("dup2 stdout failed");
                    exit(1);
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

void reportError(int ERROR_TYPE)
{

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