#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include<sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <math.h>
#include <fcntl.h>
#include<sys/types.h>





//Declaration of recursive fileSearch function
void fileSearch(char *keyword, char *current_dir, int recursive, int open);

//Declaration of cdh command helper functions
void printCdHistory(char *cdHistory[]);
void addCdToHistory(char *cd);
void writeToCdhFile();
void readFromCdhFile();

//Fixed sized string array (list) for keeping directory history
char *cdHistory[10];
//Index for reaching elements of the history list
int cdCount = 0;
//Path to directory in which shellfyre exist
char pathToShellfyre[512];

//Flags for status of module and crontab job
int module_open = 0;
int has_crontab = 0;


const char *sysname = "shellfyre";

enum return_codes
{
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t
{
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3];		// in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");
	for (i = 0; i < 3; i++)
		printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i = 0; i < command->arg_count; ++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i = 0; i < 3; ++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next = NULL;
	}
	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);
	while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
		buf[--len] = 0; // trim right whitespace

	if (len > 0 && buf[len - 1] == '?') // auto-complete
		command->auto_complete = true;
	if (len > 0 && buf[len - 1] == '&') // background
		command->background = true;

	char *pch = strtok(buf, splitters);
	command->name = (char *)malloc(strlen(pch) + 1);
	if (pch == NULL)
		command->name[0] = 0;
	else
		strcpy(command->name, pch);

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		if (len == 0)
			continue;										 // empty arg, go for next
		while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
			arg[--len] = 0; // trim right whitespace
		if (len == 0)
			continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|") == 0)
		{
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0)
			continue; // handled before

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<')
			redirect_index = 0;
		if (arg[0] == '>')
		{
			if (len > 1 && arg[1] == '>')
			{
				redirect_index = 2;
				arg++;
				len--;
			}
			else
				redirect_index = 1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 && ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}
		command->args = (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;
	return 0;
}

void prompt_backspace()
{
	putchar(8);	  // go back 1
	putchar(' '); // write empty over
	putchar(8);	  // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	// FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state = 0;
	buf[0] = 0;

	while (1)
	{
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c == 9) // handle tab
		{
			buf[index++] = '?'; // autocomplete
			break;
		}

		if (c == 127) // handle backspace
		{
			if (index > 0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c == 27 && multicode_state == 0) // handle multi-code keys
		{
			multicode_state = 1;
			continue;
		}
		if (c == 91 && multicode_state == 1)
		{
			multicode_state = 2;
			continue;
		}
		if (c == 65 && multicode_state == 2) // up arrow
		{
			int i;
			while (index > 0)
			{
				prompt_backspace();
				index--;
			}
			for (i = 0; oldbuf[i]; ++i)
			{
				putchar(oldbuf[i]);
				buf[i] = oldbuf[i];
			}
			index = i;
			continue;
		}
		else
			multicode_state = 0;

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}
	if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
		index--;
	buf[index++] = 0; // null terminate string

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

int main()
{	//

	
	if(getcwd(pathToShellfyre,sizeof(pathToShellfyre)) == NULL)
	       ("Could not get the cwd!");	


	readFromCdhFile();

	//
	while (1)
	{
		struct command_t *command = malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code == EXIT)
			break;

		code = process_command(command);
		if (code == EXIT)
			break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

int process_command(struct command_t *command)
{
	int r;
	if (strcmp(command->name, "") == 0)
		return SUCCESS;

	if (strcmp(command->name, "exit") == 0) {
		
		//Store the cdHistory to cdFile in the same directory with shellfyre.c
		if(cdCount != 0) {
			//Change directory to directory in which shellfyre exist
			r = chdir(pathToShellfyre);
			if (r == -1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			
			//Store the cdHistory in cdFile and store the file in the same directory with shellfyre.c
			writeToCdhFile();

		}

		//Removing the module it is open
		if(module_open) {

			//close(fd);	
			char *path1 = "/usr/bin/sudo";
			char *args1[] = {path1,"rmmod","process_module.ko", 0};
			pid_t pid1;

			pid1= fork();

			if(pid1 == 0) {
			//Calling rmmod in the child
				execv(path1,args1);
			}
			else {
				wait(NULL);
			}
		}
		
		//Removing the crontab job if there is any
		if(has_crontab) {
			char *user;
			user = getlogin();
			char *path = "/usr/bin/crontab";
			char *args[] = {path,"-u",user,"-r",NULL};
			pid_t pid;

			pid= fork();

			if(pid == 0) {
				execv(path,args);
			}
			else {
				wait(NULL);
			}

			remove("cronFile");
			has_crontab = 0;
		}

		return EXIT;
	}

	if (strcmp(command->name, "cd") == 0)
	{
		if (command->arg_count > 0)
		{
			r = chdir(command->args[0]);
			
			if (r == -1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));

			//Upon changing directory, add the current directory to chHistory for cdh command
			else {
			
				char cwd[512];
				if(getcwd(cwd,sizeof(cwd)) != NULL) {
					addCdToHistory(cwd);
				}
			
			}
			/////////////////
			return SUCCESS;
		}
	}

	// TODO: Implement your custom commands here
	
	if(strcmp(command->name, "filesearch") == 0) {

		int recursive = 0;
		int open = 0;
		char keyword[30];

		if(command->args[0] == NULL) {

			printf("Usage: filesearch 'keyword'. Options: -r, -o");
			return SUCCESS;
		}
		//Assigning command line options to open and recursive flags
		else if (strcmp(command->args[0],"-r") == 0) {
			recursive = 1;
			
			if(strcmp(command->args[1], "-o") == 0) {
				open = 1;
				strcpy(keyword, command->args[2]);
			}
			else {
				strcpy(keyword, command->args[1]);
			}
		}
		else if (strcmp(command->args[0],"-o") == 0) {
				open = 1;

			if(strcmp(command->args[1], "-r") == 0) {
				recursive = 1;
				strcpy(keyword, command->args[2]);
			}
			else {
				strcpy(keyword, command->args[1]);
			}
		}
		else if (command->arg_count > 1) {
			printf("filesearch: bad usage\n");
		}
		else {
			strcpy(keyword, command->args[0]);
		}
		
		//Calling fileSearch function with inputs keyword, current dir (.),and options recursive and open
		fileSearch(keyword,".", recursive, open);
		return SUCCESS;	
	}

	if(strcmp(command->name, "cdh") == 0) {

		if(command->arg_count > 0) {
			printf("cdh: Works with zero arguments.\n");
			return SUCCESS;
		}

		else if(cdCount == 0) {
			printf("No previous directories to select from!\n");
			return SUCCESS;
		}

		//Print cdHistory to user
		printCdHistory(cdHistory);

		char selected_dir[100];
		char selected_dir_main[100];
		pid_t pid;
		int pipefds[2];
		
		//Scanf the input from the child and send it to parent with pipes
		if(pipe(pipefds) == -1) {

			printf("Pipe failed!\n");
		}

		pid = fork();

		if(pid == 0) {

			printf("Select a directory by letter or number: ");
			scanf("%s",selected_dir);

			close(pipefds[0]);
			write(pipefds[1],selected_dir,(strlen(selected_dir)+1));
			exit(0);

		}
		else {
			wait(NULL);

			close(pipefds[1]);
			read(pipefds[0],selected_dir_main,sizeof(selected_dir_main));

			//Determine the index
			if(strcmp(selected_dir_main, "a") == 0) {
				strcpy(selected_dir_main, "1");
			}
			else if (strcmp(selected_dir_main, "b") == 0) {
				strcpy(selected_dir_main, "2");
			}
			else if (strcmp(selected_dir_main, "c") == 0) {
				strcpy(selected_dir_main, "3");
			}
			else if (strcmp(selected_dir_main, "d") == 0) {
				strcpy(selected_dir_main, "4");
			}
			else if (strcmp(selected_dir_main, "e") == 0) {
				strcpy(selected_dir_main, "5");
			}
			else if (strcmp(selected_dir_main, "f") == 0) {
				strcpy(selected_dir_main, "6");
			}
			else if (strcmp(selected_dir_main, "g") == 0) {
				strcpy(selected_dir_main, "7");
			}
			else if (strcmp(selected_dir_main, "h") == 0) {
				strcpy(selected_dir_main, "8");
			}
			else if (strcmp(selected_dir_main, "i") == 0) {
				strcpy(selected_dir_main, "9");
			}
			else if (strcmp(selected_dir_main, "j") == 0) {
				strcpy(selected_dir_main, "10");
			}

			int index = atoi(selected_dir_main);	
			
			//Change directory and add it to cdHistory
			r = chdir(cdHistory[cdCount - index]);
			if (r == -1) {
				printf("Please provide a valid number or letter!\n");
			}
			else {
			
				char cwd[512];
				if(getcwd(cwd,sizeof(cwd)) != NULL) {
					addCdToHistory(cwd);
				}
			
			}


		}
		return SUCCESS;
	}

	if(strcmp(command->name, "take") == 0) {
	
		int r1,r2;
		if(command->arg_count > 1) {
			printf("take: Too many arguments.");
			printf("Usage: take [DIRECTORY]");
			return SUCCESS;
		}
		//Tokenizing the argument of take command
		char *input = strdup(command->args[0]);
		char *token = strtok(input, "/");
		
		//for each token 
		while( token != NULL ) {
			
			//make directory named token
			r1 = mkdir(token, S_IRWXU);
			if(r1 == -1) {
				if(errno != EEXIST) {

					printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
					return SUCCESS;
				}
			}
			//change directory to directory named token
			r2 = chdir(token);
			if (r2 == -1) {
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
				return SUCCESS;
			}
			//and add every directory changes to cdHistory
			else {
			
				char cwd[512];
				if(getcwd(cwd,sizeof(cwd)) != NULL) {
					addCdToHistory(cwd);
				}
			
			}

			token = strtok(NULL, "/");
		}
		return SUCCESS;
	}



	if(strcmp(command->name, "joker") == 0) {

		char currentPath[100];

		if(getcwd(currentPath,sizeof(currentPath)) == NULL)
			printf("Could not get the cwd!\n");

		//Change directory to store cronFile in the the same directory with shellfyre.c
		int r1 = chdir(pathToShellfyre);
			if (r1 == -1) 
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));

		//opening the cronFile at writing the job
		FILE *cronFile = fopen("cronFile", "w");
		fprintf(cronFile,"*/15 * * * * XDG_RUNTIME_DIR=/run/user/$(id -u) /usr/bin/notify-send \"JOKE\" \"$(/snap/bin/curl https://icanhazdadjoke.com/)\"\n");
		fclose(cronFile);


		//calling crontab in the child with cronFile
		char *path = "/usr/bin/crontab";
		char *args[] = {path,"cronFile",NULL};

		pid_t pid = fork();

		if(pid == 0) {
		execv(path,args);
		}
		else {
			wait(NULL);
		}

		int r2 = chdir(currentPath);
		if (r2 == -1) 
			printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
		
		has_crontab = 1;
		return SUCCESS;
	}

	if(strcmp(command->name, "madmath") == 0) {
		
		if(command->arg_count < 1 ) {

			printf("math :Too few arguments\n");
			printf("usage: math <option> <number> <number>\n");
			printf("options :sub ,sum,factor !!!!!FIND OUT THE REST!!!!!!!\n");
		}
		//Assigning options
		else if(strcmp(command->args[0],"sub") == 0){
		    
		    if(command->args[1] == NULL || command->args[2] == NULL) {

				printf("math: sub: bad usage\n");
				printf("usage: math sub <num1> <num2>\n");
		    }
		    else {
                		int num1 = atoi(command->args[1]);
    				int num2 = atoi(command->args[2]);
    				int sub = num1 - num2;
    				printf("%d\n",sub);
		    }	
		}
		else if(strcmp(command->args[0],"sum") == 0){
		    
		    if(command->args[1] == NULL || command->args[2] == NULL) {

				printf("math: sum: bad usage\n");
				printf("usage: math sum <num1> <num2>\n");
			}
			else {
    		    		int num1 = atoi(command->args[1]);
    				int num2 = atoi(command->args[2]);
    				int sum = num1 + num2;
    				printf("%d\n",sum);
			}
		}
		else if(strcmp(command->args[0],"factor") == 0) {

			int sum = 1;
			if(command->args[1] == NULL) {
				printf("Please provide a number!\n");

			}
			else if(command->arg_count > 2) {
				printf("math: factor: bad usage\n");
				printf("usage: math factor <num> \n");;
			}

			else if(command->args[1] < 0)
        			printf("Factoriel of a negative number cannot be calculated!\n");

    			else {
        			for (int i = 1; i <= atoi(command->args[1]); ++i) {
           				 sum *= i;
        			}

				printf("%d\n",sum);
			}
			
         	}

		else if(strcmp(command->args[0],"pi") == 0) {

			long double pi = M_PI;	
    			printf("%.10Lf\n", pi);

		}
		
		else if(strcmp(command->args[0],"pow") == 0) {

			if(command->args[1] == NULL || command->args[2] == NULL) {

				printf("math: pow: bad usage\n");
				printf("usage: math pow <base> <power>\n");
			}
			else {  int result = 1;
    				int base = atoi(command->args[1]);
    				int power = atoi(command->args[2]);
    				for (power; power>0; power--){
    
    					result = result * base;
    				}
				printf("%d\n",result );
            		}

		}

		else if(strcmp(command->args[0],"mod") == 0) {

			if(command->args[1] == NULL || command->args[2] == NULL) {

				printf("math: mod: bad usage\n");
				printf("usage: math mod <num1> <num2>\n");
			}
			else {
    		   		int num1 = atoi(command->args[1]);
    				int num2 = atoi(command->args[2]);
    				int mod = num2 % num1;
    				printf("%d\n",mod);
			}
		}
		
		//Chosing the message to be displayed randomly
		char *header,*message;
		int num;
		num = rand() % 11;
		
		switch (num) {
		    
		    case 1:
		        header = "BEST REGARDS";
		        message = "FROM YOUR MIDDLE SCHOOL MATH TEACHER";
		        break;

		    case 2:
		        header = "Tesla Business Offer";
		        message = "HEY DUDE, THIS IS ELON WANT U";
		        break;
		    
		    case 3:
		        header = "NASA";
		        message = "WANNA BECOME A ASTRONAUT";
		        break;
		    
		    case 4:
		        header = "Leonhard Euler";
		        message = "I am proud of you son!";
		        break;
		        
		    case 5:
		        header = "Abel Prize";
		        message = "We want to give you The Abel Prize sir";
		        break;
		    
		    case 6:
		        header = "***CONGRATS***";
		        message = "YOU ARE CHOOSEN AS GOAT MATHEMATICIAN!";
		        break;    
		    
		    case 7:
		        header = "Pythagoras";
		        message = "Like your triangle right triangle";
		        break; 
		        
		    case 8:
		        header = "Guinness World Records";
		        message = "NEW WORLD RECORD: The most genius mathematician is you!!!";
		        break; 
		    case 9:
		        header = "***FATAL WARNING***";
		        message = "I AM NOT A CALCULATOR, I AM A COMPUTER!!!";
		        break; 
		    default:
		        header = "***FATAL WARNING***";
		        message = "I AM NOT A CALCULATOR, I AM A COMPUTER!!!";
		        break;   
    		}  
		 
		//Displaying the message with the notify-send    		 
		char *path = "/usr/bin/notify-send";
		char *args[] = {path,header,message,NULL};
		pid_t pid;

		pid = fork();

		if(pid == 0) {
			execv(path,args);
		}
		else {
			wait(NULL);
		}	
		
		return SUCCESS;
	}
	
	if(strcmp(command->name, "pstraverse") == 0) {

		//module parameters
		char PID[20] ="PID=";
		char option[20];
		char data[100];


		if(command->arg_count < 2) {
			("pstravers: Too few arguments.\n");
			("Usage: pstraverse <PID> <-d or -b>\n");

			return SUCCESS;
		}

		//assigning module paramters
		strcat(PID,command->args[0]);
		sprintf(option, "option=\"%s\" ", command->args[1]);


		//if device is not open then opening or installing it
		if(!module_open) {
			//Path and argument resolving
			char *path = "/usr/bin/sudo";
			char *args[] = {path,"insmod","process_module.ko",PID,option,NULL};

			pid_t pid = fork();

			if(pid == 0) {

			//Calling in the child
			execv(path,args);

			}
			else {
				wait(NULL);
			}
			module_open = 1;

		}
		else {
			int fd;
			fd = open("/dev/process_device", O_RDWR);

			if(fd < 0) {
				printf("-%s: %s\n", sysname,strerror(errno));
				printf("Cannot open the file\n");
			}
		       	strcat(data,command->args[0]);
			strcat(data, " ");
			strcat(data, command->args[1]);	
			write(fd,data, strlen(data) +1);
			close(fd);
		}
		strcpy(data,"");
		return SUCCESS;
	}


	pid_t pid = fork();

	if (pid == 0) // child
	{
		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;


		//path resolving and calling execv
		char path[1024] = "/bin/";
		strcat(path,command->name);
		execv(path,command->args);
	}
	else {
		//Waiting for child to finish if command is not running in background
		if(command->args[command->arg_count-2] != "&") {
			wait(NULL);
		}
		return SUCCESS;
	}

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}


void fileSearch(char *keyword, char *current_dir,int recursive, int open) {
	struct dirent *dir;
	DIR *d = opendir(current_dir);
	char next_dir[512];
	char directory[512];

	if(d != NULL) {

		while((dir = readdir(d)) != NULL) {
		
		//If it is current or previous directory then ignore otherwise creates infinite loop
			if(strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {			

				if(strstr(dir->d_name,keyword)) {
	
					printf("%s/%s\n",current_dir,dir->d_name);

					if (open) {
						
						//directory resolving
						strcpy(directory,current_dir);
						strcat(directory,"/");
						strcat(directory,dir->d_name);
						
						//Calling xdg-open in the child	
						char *path = "/bin/xdg-open";
						char *args[] = {path,directory,NULL};
						pid_t pid = fork();

						if(pid == 0) {

							execv(path, args);
						}
						else {
							wait(NULL);
						}
					}
				}
				
				//Next_dir resolving and calling recursively
				if(recursive) {
					strcpy(next_dir,current_dir);
					strcat(next_dir,"/");
					strcat(next_dir,dir->d_name);
					fileSearch(keyword, next_dir,recursive,open);

				}

			}
				
		}
		closedir(d);
	}
	return;	
}
/**
  * Adds given directory to cdHistory list 
  * @param cd 
  * */
void addCdToHistory(char *cd) {

	if(cdCount < 10) {

		cdHistory[cdCount] = strdup(cd);
		cdCount++;
	}
	else {
		free(cdHistory[0]);
		for(int i = 1; i < 10; i++) {
			cdHistory[i - 1] = cdHistory[i];
			
		}
		cdHistory[9] = strdup(cd);
	}
}
/**
  * Prints the recenlty visited directories for a given list 
  * @param cdHistory[]
  * */
void printCdHistory(char *cdHistory[]) {
	char letters[10] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'};
	for(int i = 0; i < cdCount; i++) {
       		printf("%c %d) %s\n",letters[cdCount-i-1], cdCount-i, cdHistory[i]);
        }
	

}
/**
  * Writes the content of the cdHistory to the file 
  * */
void writeToCdhFile() {
	FILE *fp; 
	fp = fopen("cdhFile","w+");
	if(fp == NULL) {
		printf("Could not open the cdhFile!\n");
	}

	for(int i = 0; i < 10; i++) {
		if(cdHistory[i] != NULL)
			fprintf(fp, "%s\n",cdHistory[i]);
		else
			break;
	}
	fclose(fp);

}
/**
  * Reads the content of the cdHistory to the file 
  * */
void readFromCdhFile() {

	if(access("cdhFile", F_OK) != 0) {
		return;
	}
	FILE *fp; 
	int i = 0;
	char line[100];
	fp = fopen("cdhFile","r");
	if(fp == NULL) {
		printf("Could not open the cdhFile!");
		fclose(fp);
		return;

	}

	while((fscanf(fp, "%s", line) == 1)) {
		addCdToHistory(line);
	}
	fclose(fp);

}

