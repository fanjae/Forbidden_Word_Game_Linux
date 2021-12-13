#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <pthread.h>
#include "/usr/include/mysql/mysql.h"

#define ROOM_SIZE 5
#define BUF_SIZE 110
#define EPOLL_SIZE 50
#define DEBUG_MODE 1
typedef struct client_list
{
	char id[10];
	struct sockaddr_in clnt_adr;
	int connect;
	int wincount;
}client_list;

typedef struct chat_room_information
{
	int *serv_sock;
	int room_number;
	int port;
	char name[10];	
}information;

typedef struct db_client
{
	int *sock_copy;
	struct sockaddr_in clnt_adr;
	char message[BUF_SIZE];
	char id[10];
	int wincount;
}db_client;

typedef struct room_list
{
	int on;
	char name[10];
	pthread_t tid;
	int port;
//	int serv_sock;
	int clnt_cnt;
}room_list;

void *lobby(void *arg);
void *room_make(void *arg);
void *db_login(void *arg);
void *db_create(void *arg);
void *chatting_room(void *arg);
void add_client(char *id,struct sockaddr_in clnt_adr,int connect,int wincount);
void sendto_all(int *serv_sock, char *message, int size, client_list *user, struct sockaddr_in *clnt_adr,int room_number);
void error_handling(char *message);
int next_user(client_list *user, struct sockaddr_in *clnt_adr, int user_number);
int target_user(client_list *user, struct sockaddr_in *clnt_adr);
void append(char *dst, char c); 

pthread_mutex_t login_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t room_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_t thread_address[ROOM_SIZE*4]={0};
client_list client[ROOM_SIZE*4]={0};
db_client save_db[ROOM_SIZE*4]={0};
room_list room[ROOM_SIZE]={0};

int room_cnt=0,user_cnt=0;
int *sock_db_copy;
void line();
int main(int argc, char *argv[])
{
	int serv_sock;
	char message[BUF_SIZE];
	int str_len;
	int port;
	socklen_t clnt_adr_sz;
	socklen_t clnt_adr_user_sz;

	struct sockaddr_in serv_adr, clnt_adr;


	if(argc!=2){
		printf("Usage : %s <port>\n", argv[0]);
		exit(1);
	}
	
	serv_sock=socket(PF_INET, SOCK_DGRAM, 0);
	if(serv_sock==-1) // 서버 연결을 거친다.
		error_handling("UDP socket creation error");
	printf("sock : %d\n",serv_sock);	 
   
	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family=AF_INET;
	serv_adr.sin_addr.s_addr=htonl(INADDR_ANY);
	serv_adr.sin_port=htons(atoi(argv[1]));
	port = atoi(argv[1]);
	
	if(bind(serv_sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr))==-1)
		error_handling("bind() error");
	sock_db_copy = &serv_sock;


	while(1) {
		char type[3];
		clnt_adr_sz = sizeof(clnt_adr);
		str_len = recvfrom(serv_sock, message, BUF_SIZE, 0, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);
		message[str_len] = 0;

		if(DEBUG_MODE) 
			printf("%s",message);

		type[0] = message[6];
		type[1] = message[7];
		type[2] = '\0';
		
		if(DEBUG_MODE)
			line();
			printf("log : Data Type : %s\n",type);
			line();	

		if(strcmp(type,"01") == 0) {
			pthread_t db_tid;
			db_client db_temp;
			db_temp.sock_copy = &serv_sock;
			db_temp.clnt_adr = clnt_adr;
			strcpy(db_temp.message,message);
			pthread_create(&db_tid, NULL, (void *)db_login,(void *)&db_temp);
		}				

		if(strcmp(type,"02") == 0) {
			pthread_t db_tid;
			db_client db_temp;
			db_temp.sock_copy = &serv_sock;
			db_temp.clnt_adr = clnt_adr;						
			strcpy(db_temp.message,message);
			pthread_create(&db_tid, NULL, (void *)db_create,(void *)&db_temp);		
		}

		if(strcmp(type,"03") == 0) {
			if(room_cnt > 5) {
				line();
				printf("Log : Server Room It's Maximum\n");
				line();
			}
			else {
				int msg_len = strlen(message);
				int port_copy = 0;
				char room_name[10]={0};
				
				for(int i = 10; i < msg_len; i++) 
					append(room_name,message[i]);
	
				line();
				printf("Log : Room name : %s\n",room_name);
				line();
				information infor_room;
				port_copy = port + room_cnt +1;
				infor_room.port = port_copy;
				infor_room.room_number = room_cnt;
				strcpy(infor_room.name,room_name);		
				
			pthread_create(&room[room_cnt].tid,NULL,(void *)chatting_room,(void *)&infor_room);
			line();
			printf("Log : Ip : %s port : %d Create New Room : name : %s\n",inet_ntoa(clnt_adr.sin_addr),clnt_adr.sin_port,room_name);
			line();
			}
		}
		if(strcmp(type,"04") == 0) {
			char temp[BUF_SIZE];
			char room_data[3];
			int number;
			line();
			printf("message : %s\n",message);
			line();
			room_data[0] = message[0];
			room_data[1] = message[1];
			number = (room_data[0] - '0') * 10;
			number = (room_data[1] - '0')-1;
			line();
			printf("number : %d\n",number);
			printf("port : %d\n",room[number].port);
			line();
			sprintf(temp,"0000000400%d",room[number].port);
			if(DEBUG_MODE) {
				line();
				printf("Send message : ");
				printf("%s\n",temp);
				line();
			}

			sendto(serv_sock,temp,BUF_SIZE,0,(struct sockaddr*)&clnt_adr,clnt_adr_sz);					

		}
			
	}	
	return 0;
}
void *lobby(void *arg)
{
	db_client new_db = *(db_client *)arg; 
	thread_address[user_cnt-1] = pthread_self();
	if(DEBUG_MODE) {
		line();
		printf("ip : %s port : %d\n",inet_ntoa(new_db.clnt_adr.sin_addr),new_db.clnt_adr.sin_port);
		line();
	}
	while(1) {
		char lobby_message[110]={0};
		if(room_cnt == 0) {
			sprintf(lobby_message,"0000011100");
		}
		else {
			int clnt_adr_uz_size = sizeof(new_db.clnt_adr);
			sprintf(lobby_message,"00%d%d011100",room_cnt/10,room_cnt%10);
			
			for(int i = 0; i < room_cnt; i++)
			{
				int len = strlen(room[i].name);
				for(int j = len-1; j < 9; j++)
					strcat(room[i].name," ");
				strcat(lobby_message,room[i].name);
				len = strlen(room[i].name);
			}
			line();
			printf("Log : lobby : %s\n",lobby_message);
			line();
			sendto(*new_db.sock_copy,lobby_message,110,0,(struct sockaddr*)&new_db.clnt_adr,clnt_adr_uz_size);
		}
		sleep(5);
	}
}
void *db_login(void *arg) // db 생성
{
	int cnt;
	int str_len; 
	int type=0;
	int clnt_adr_uz_size;	

	char id[20];
	char password[20];
	char sql_message[200];
	char message[BUF_SIZE+10];
	
	db_client db_temp = *(db_client *)arg;
	
/*
	db_temp->sock_copy = db_set.sock_copy;
	db_temp->clnt_adr.sin_addr = db_set->clnt_adr.sin_addr;
	db_temp->clnt_adr.sin_port = db_set->clnt_adr.sin_port;
*/

	clnt_adr_uz_size = sizeof(db_temp.clnt_adr);
	strcpy(message,db_temp.message);

	if(DEBUG_MODE) {
		printf("Log : db_login thread ip : %s port : %d\n",inet_ntoa(db_temp.clnt_adr.sin_addr),db_temp.clnt_adr.sin_port);
	}

	for(int i = 10; i < strlen(message)-1; i++) {
		if(message[i] == '|')
		{
			if(type == 0) {
				type++;
				continue;
			}
		}
		else  {
			if(type == 0) 
				append(id,message[i]);	
			else
				append(password,message[i]);
		}
	}

	MYSQL *conn = mysql_init(NULL);
	if(conn == NULL) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		return NULL;
	}
	
	if(mysql_real_connect(conn, "localhost", "fanjae", "fanjae1123","network_db",0,NULL,0) == NULL) {
		fprintf(stderr, "connection error");
		return NULL;
	}

	sprintf(sql_message,"SELECT * from network WHERE id=\'%s\' && password = \'%s\';",id,password);
	if(mysql_query(conn, sql_message))
	{
		fprintf(stderr,"log error : SELECT Query error\n");
		return NULL;
	}
	MYSQL_RES *result = mysql_store_result(conn);
	int num_fields = mysql_num_fields(result);
	MYSQL_ROW row;
	if((row = mysql_fetch_row(result)) == NULL) {
		fprintf(stderr,"log error : ID or Password Incorrect.\n");
		line();
		printf("Log : Login Fail ip : %s port : %d\n",inet_ntoa(db_temp.clnt_adr.sin_addr),db_temp.clnt_adr.sin_port);
		line();
		sendto(*db_temp.sock_copy,"0000000000Error : ID or Password Incorrect.\n", BUF_SIZE, 0, (struct sockaddr*)&db_temp.clnt_adr,clnt_adr_uz_size);
		return NULL;
	}	
	else {
		strcpy(db_temp.id,id);
		db_temp.wincount = atoi(row[3]);		

		pthread_mutex_lock(&login_lock);
		add_client(id,db_temp.clnt_adr,1,db_temp.wincount);
		pthread_mutex_unlock(&login_lock);

		pthread_t lobby_list_tid;
		pthread_t lobby_go;
		line();
		printf("Log : Login Success ip : %s port : %d\n",inet_ntoa(db_temp.clnt_adr.sin_addr),db_temp.clnt_adr.sin_port);
		line();
		sendto(*db_temp.sock_copy,"0000000100Login Success!!\n",BUF_SIZE,0,(struct sockaddr*)&db_temp.clnt_adr,clnt_adr_uz_size);
		
		if(DEBUG_MODE)
			printf("lobby!\n");
	
		line();
		printf("Log : lobby Go ip : %s port : %d\n",inet_ntoa(db_temp.clnt_adr.sin_addr),db_temp.clnt_adr.sin_port);	
		line();

		strcpy(save_db[user_cnt-1].id,id);
		save_db[user_cnt-1].wincount = atoi(row[3]);

		save_db[user_cnt-1].sock_copy = db_temp.sock_copy;
		save_db[user_cnt-1].clnt_adr.sin_addr = db_temp.clnt_adr.sin_addr;
		save_db[user_cnt-1].clnt_adr.sin_port = db_temp.clnt_adr.sin_port;

		pthread_create(&lobby_list_tid,NULL,lobby,(void *)&save_db[user_cnt-1]);
		
		return NULL;
	}

}

void add_client(char *id, struct sockaddr_in clnt_adr, int connect,int wincount)
{
	user_cnt++;
	for(int i = 0; i < user_cnt; i++) {
		if(client[i].connect == 0)
		{
			printf("client new id : %s\n",id);
			strcpy(client[i].id,id);
			client[i].clnt_adr.sin_addr.s_addr = clnt_adr.sin_addr.s_addr;
			client[i].clnt_adr.sin_port = clnt_adr.sin_port;
			client[i].connect = 1;	
			client[i].wincount = wincount;
			printf("LOG : %d번째 접속\n",i+1);
			if(DEBUG_MODE) {
				printf("LOG : Login Information ip : %s port : %d\n",inet_ntoa(client[i].clnt_adr.sin_addr),client[i].clnt_adr.sin_port);
			}
			break;
		}
	}	
}

void *db_create(void *arg) // 계정 생성
{
	int cnt;
	int str_len; 
	int type=0;
	int clnt_adr_uz_size;	

	char id[20];
	char password[20];
	char sql_message[200];
	char message[BUF_SIZE+10];

	db_client db_temp;
	memcpy(&db_temp,(db_client *)arg, sizeof(db_temp));
	clnt_adr_uz_size = sizeof(db_temp.clnt_adr);
	strcpy(message,db_temp.message);


	for(int i = 10; i < strlen(message)-1; i++) {
		if(message[i] == '|')
		{
			if(type == 0) {
				type++;
				continue;
			}
		}
		else  {
			if(type == 0) 
				append(id,message[i]);	
			else
				append(password,message[i]);
		}
	}

	MYSQL *conn = mysql_init(NULL);
	if(conn == NULL) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		return NULL;
	}
	
	if(mysql_real_connect(conn, "localhost", "fanjae", "fanjae1123","network_db",0,NULL,0) == NULL) {
		fprintf(stderr, "connection error");
		return NULL;
	}

	sprintf(sql_message,"SELECT id from network WHERE id=\'%s\';",id);
	if(mysql_query(conn, sql_message))
	{
		fprintf(stderr,"log error : SELECT Query error\n");
		return NULL;
	}
	MYSQL_RES *result = mysql_store_result(conn);
	int num_fields = mysql_num_fields(result);
	MYSQL_ROW row;

	if((strcmp(id,"") == 0) || strcmp(password,"") == 0)
	{
		fprintf(stderr,"log error : DB_CANNOT_USER_CREATE(Null)\n");
		sendto(*db_temp.sock_copy,"0000000000Error : ID or Password it's null.\n",BUF_SIZE,0,(struct sockaddr*)&db_temp.clnt_adr,clnt_adr_uz_size);
		return NULL;
	}
	if((row = mysql_fetch_row(result)) == NULL) {
		sprintf(sql_message,"INSERT INTO network VALUES(NULL, \'%s\', \'%s\', 0)",id,password);

		if(DEBUG_MODE) {
			printf("SQL Query Message : %s\n",sql_message);
		}
		if(mysql_query(conn, sql_message))
		{
			fprintf(stderr,"log error : DB_CANNOT_USER_CREATE\n");
			sendto(*db_temp.sock_copy,"0000000000Error : Account error!\n",BUF_SIZE,0,(struct sockaddr*)&db_temp.clnt_adr,clnt_adr_uz_size);
			return NULL;
		}
		fprintf(stderr,"log : DB_USER CREATE\n");
		sendto(*db_temp.sock_copy,"0000000200Account Create Success!\n",BUF_SIZE,0,(struct sockaddr*)&db_temp.clnt_adr,clnt_adr_uz_size);
		return NULL;
	}	
	else {
		fprintf(stderr,"log error : DB_USER Exist\n");
		sendto(*db_temp.sock_copy,"0000000000Error : This ID Exist\n",BUF_SIZE,0,(struct sockaddr*)&db_temp.clnt_adr,clnt_adr_uz_size);
		return NULL;
	}
}
char *find_name(int i)
{
	return client[i].id;
}
void *chatting_room(void *arg)
{
	client_list *user;
	information infor = *(information *)arg;
	line();
	printf("Log : User in room name : %s\n",infor.name);
	line();
	if(room[infor.room_number].on == 0) {
		room[infor.room_number].on = 1;
		room[infor.room_number].clnt_cnt++;
		room[infor.room_number].tid = pthread_self();
		room[infor.room_number].port = infor.port;
		strcpy(room[infor.room_number].name,infor.name);
		
		pthread_mutex_lock(&room_lock);
		room_cnt++; 
		printf("Log : Now room_cnt : %d\n",room_cnt);
		pthread_mutex_unlock(&room_lock);
		
		user = (struct client_list *) malloc(sizeof(struct client_list) * 4);
	}
	int serv_sock;
	char message[BUF_SIZE];
	int str_len;
	int port = infor.port;
	struct sockaddr_in serv_adr, clnt_adr;
	socklen_t clnt_adr_sz;

	serv_sock = socket(PF_INET, SOCK_DGRAM, 0);
	printf("serv_sock(new) : %d\n",serv_sock);
	if(serv_sock == -1)
		error_handling("UDP socket creation error");
	
	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family = AF_INET;
	serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_adr.sin_port = htons(port);

	if(bind(serv_sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
		error_handling("bind() error");

	while(1) 
	{
		int find = 0;
		clnt_adr_sz=sizeof(clnt_adr);
		str_len=recvfrom(serv_sock, message, BUF_SIZE, 0, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);
		message[str_len-1] = 0;
		if((strcmp(message,"0000000000exit\n") == 0) || (strcmp(message,"0000000000exit") == 0)) {
			sendto_all(&serv_sock,message,BUF_SIZE,user,&clnt_adr,infor.room_number);
			continue;
		}
		
		if((strcmp(message,"0000000500start\n") == 0) || (strcmp(message,"0000000500start") == 0)) {
			for(int i = 0; i < 4; i++) {	
				if(user[i].connect == 1) {
					char end[110];
					int new_clnt_adr_sz = sizeof(user[i].clnt_adr);
					int next = next_user(user,&clnt_adr,i);
					sprintf(end,"0%d00000000%dp의 금칙어를 설정하세요.",i,next);
					sendto(serv_sock,"0%d00000500%dp의 금칙어를 설정하세요.",BUF_SIZE,0,(struct sockaddr*)&user[i].clnt_adr,new_clnt_adr_sz);
				}
			}
		}
		for(int i = 0; i < 4; i++)
		{
			if(user[i].connect == 1 && (user[i].clnt_adr.sin_addr.s_addr == clnt_adr.sin_addr.s_addr) && (user[i].clnt_adr.sin_port == clnt_adr.sin_port))
			{
				find = 1;
				break;
			}
		} 
		char type_check[3];
		type_check[0] = message[6];
		type_check[1] = message[7];
		type_check[2] = '\0';
		if(strcmp(type_check,"07") == 0) {
			sprintf(message,"%dp is death.\n",(int)message[1]-49);					
			sendto_all(&serv_sock,message,BUF_SIZE,user,&clnt_adr,infor.room_number);
		}
		for(int i = 0; i < 4; i++)
		{
			if(find == 0 && user[i].connect == 0 && user[i].clnt_adr.sin_addr.s_addr == 0)
			{
				if((strcmp(message,"0000009900\n") == 0) || (strcmp(message,"0000009900") == 0)) {
					char user_infor_message[BUF_SIZE];
					room[infor.room_number].clnt_cnt++; 
					user[i].clnt_adr.sin_addr.s_addr = clnt_adr.sin_addr.s_addr;
					user[i].clnt_adr.sin_port = clnt_adr.sin_port;
					user[i].connect = 1;
					printf("log : %dp login\n",i+1);
					sendto(serv_sock,"test",BUF_SIZE+10,0,(struct sockaddr*)&user[i].clnt_adr,clnt_adr_sz);
					printf("%d\n",room[infor.room_number].clnt_cnt-1);
				 	sprintf(user_infor_message,"0%d00009800",room[infor.room_number].clnt_cnt-1);
					printf("YES\n");
					for(int j = 0; j < 4; j++) {
						int clnt_adr_user_uz = sizeof(user[j].clnt_adr);
						if(user[j].connect == 1) {
							char tempid[10];
							strcpy(tempid,find_name(j));
							printf("id : %s\n",tempid);
							int tempid_len = strlen(tempid);
							for(int k=tempid_len-1; k < 9; k++) {
								strcat(tempid," ");
							}
							strcat(user_infor_message,tempid);
							printf("login from message :%s\n",user_infor_message);
							sendto(serv_sock,user_infor_message,BUF_SIZE+10,0,(struct sockaddr*)&user[j].clnt_adr,clnt_adr_user_uz);
						}
					}
				
				//printf("ip : %s port : %d\n",inet_ntoa(user[i].clnt_adr.sin_addr),clnt_adr.sin_port);
				//printf("%d\n",user[i].connect);
				break;
				}
			}
		}
		/*
		for(int i = 0 ; i < BUF_SIZE ; ++i)
			if((message[i] >= 'a' && message[i] <= 'z') || message[i] == ' ' )
				continue;
			else
				message[i] = ' ';
		*/
		printf("log : send ip : %s port : %d\n",inet_ntoa(clnt_adr.sin_addr),clnt_adr.sin_port);
		sendto_all(&serv_sock, message, BUF_SIZE, user, &clnt_adr,infor.room_number);
	}	
}
int next_user(client_list *user, struct sockaddr_in *clnt_adr,int user_number)
{
	for(int i = user_number; ; i++) {
		if(user[i%4].clnt_adr.sin_addr.s_addr != clnt_adr -> sin_addr.s_addr && user[i].clnt_adr.sin_port != clnt_adr->sin_port && user[i%4].connect != 0 && user[i%4].connect == 1)
		{
			return i % 4;		
		}
	}
}
int target_user(client_list *user, struct sockaddr_in *clnt_adr)
{
	for(int i = 0; i < 4; i++) {
		if(user[i].clnt_adr.sin_addr.s_addr == clnt_adr -> sin_addr.s_addr && user[i].clnt_adr.sin_port == clnt_adr->sin_port) {
			return i + 1;
		}
	}
	return -1;
}

// Server 메시지 전달 모듈.
void sendto_all(int *serv_sock, char *message, int size, client_list *user,struct sockaddr_in *clnt_adr,int room_number) // 모든 인원에게 메시지 전달.
{
	int senduser = target_user(user,clnt_adr);
	char temp[BUF_SIZE];
	if((strcmp(message,"0000000000exit\n") == 0) || (strcmp(message,"0000000000exit") == 0)) {
		room[room_number].clnt_cnt--;
		user[senduser-1].clnt_adr.sin_addr.s_addr = 0;
		user[senduser-1].clnt_adr.sin_port = 0;
		user[senduser-1].connect = 0;
		sprintf(temp,"0000000600System : %dp is logout\n",senduser);
		printf("log : %dp logout\n",senduser);
	}
	else
		sprintf(temp,"0000000600%dp : %s",senduser,message);
	
	for(int i = 0; i < 4; i++) {
		int clnt_adr_user_sz = sizeof(user[i].clnt_adr);

		if(user[i].connect == 1) {
			printf("Log : Ok It's Send it. ip : %s port : %d, temp : %s\n",inet_ntoa(user[i].clnt_adr.sin_addr),user[i].clnt_adr.sin_port,temp);
			sendto(*serv_sock,temp,BUF_SIZE,0,(struct sockaddr*)&user[i].clnt_adr,clnt_adr_user_sz);	
		}
	}
}

void append(char *dst, char c) {
	char *p = dst;
	while (*p != '\0') p++;
	*p = c;
	*(p+1) = '\0';
}
void error_handling(char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}

void line()
{
	printf("=============================================\n");
}
