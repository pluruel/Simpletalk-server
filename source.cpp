#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <mutex>
#include <time.h>
#include "header.h"


// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 512
#define LIM_CONNECT 3

struct Users {
	int partner_idx = -1; // 파트너의 메모리 값
	int passcode = -1; // 정상적인 데이터 교환 확인을 위한 패스코드
	int amount_black_list = 0;

	bool chatting_now = false; // 다른 유저와의 채팅중인지를 확인하기 위한 함수
	bool waiting_now = false; // 기다리는 중
	bool entering_now = false; // 접속 후 waiting 이전의 상태
	char Users_id[20];
	bool can_match[LIM_CONNECT+2];
	SOCKET socket;

	std::thread *entering;
	std::thread *recv;
};

sockaddr_in sock_name;
DWORD times = GetTickCount64(); // rand()의 시드값 조절을 위한 시간 함수.
std::mutex mtx;
int seed;
int connected;
int connect_list[LIM_CONNECT + 1]; // user struct의 index값을 저장해 두는 곳 순환 queue 방식
int connect_top;
int connect_rear;
int waiting_list[LIM_CONNECT + 1]; // 대화상대가 올때까지 대기자의 index를 저장하는 곳
int waiting_num; // 기다리는 사람 수.
int matchable_no;
struct Users userlist[LIM_CONNECT + 1]; // 유저 struct의 개수. header에서 조절이 가능하다.

SOCKET initialize_listen() {
	WSADATA wsaData;
	int iResult;
	struct addrinfo *result = NULL;
	struct addrinfo hints;
	SOCKET ListenSocket = INVALID_SOCKET;

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed with error: %d\n", iResult);
		return 1;
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed with error: %d\n", iResult);;
		return 1;
	}

	// Create a SOCKET for connecting to server
	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
		printf("socket failed with error: %ld\n", WSAGetLastError());
		return 1;
	}

	// Setup the TCP listening socket
	iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		return 1;
	}

	freeaddrinfo(result);
	printf("ListenSocket is initialied\n");
	return ListenSocket;
}

int checking_info_server() {
	char input[512];
	while (1) {
		scanf("%s", &input);
		if (strcpy(input, "/amount")) {
			printf("%d명 접속중\n", connected);
		}
	}
}

void booking(int idx1, int idx2) {
	int i, temp;
	int length;
	char sendbuf[DEFAULT_BUFLEN];
	int iSendResult;
	char encrypt_string[DEFAULT_BUFLEN] = { 0, };

	srand((int)GetTickCount64());
	for (i = 0; i < 50; i++) {
		temp = rand();
		if (rand() % 2 == 0)
			temp += 32768;
		memcpy(&encrypt_string[i * 2], &temp, 2);
	}
	encrypt_string[100] = 0;
	length = 100;

	userlist[idx1].waiting_now = false;
	userlist[idx2].waiting_now = false;
	userlist[idx1].chatting_now = true;
	userlist[idx2].chatting_now = true;
	userlist[idx1].partner_idx = idx2;
	userlist[idx2].partner_idx = idx1;
	//making sendpacket



	// Making send packet
	sendbuf[0] = SYS_DATA;
	memcpy(&sendbuf[1], &cons.length[TURN_ON], 4);
	memcpy(&sendbuf[5], &cons.sentence[TURN_ON], cons.length[TURN_ON]);
	sendbuf[5 + cons.length[TURN_ON]] = 0;
	// Send stop waiting code
	iSendResult = send(userlist[idx1].socket, sendbuf, 5 + cons.length[TURN_ON], 0);
	if (iSendResult == SOCKET_ERROR) {
		err_send(iSendResult);
	}
	iSendResult = send(userlist[idx2].socket, sendbuf, 5 + cons.length[TURN_ON], 0);
	if (iSendResult == SOCKET_ERROR) {
		err_send(iSendResult);
	}

	memcpy(&sendbuf, &encrypt_string, 100);
	iSendResult = send(userlist[idx1].socket, sendbuf, 100, 0);
	if (iSendResult == SOCKET_ERROR) {
		err_send(iSendResult);
	}
	iSendResult = send(userlist[idx2].socket, sendbuf, 100, 0);
	if (iSendResult == SOCKET_ERROR) {
		err_send(iSendResult);
	}
	// Send users messages to know being connected
	return;
}


void search_new_partner() {
	int i, j, k, l;
	int idx1 = -1, idx2 = -1;
	bool judge = true;
	bool are_matched = false;
	for (i = matchable_no; i < waiting_num && are_matched == false; i++) {
		for (j = 0; j < i && are_matched == false; j++) {
			if (userlist[waiting_list[i]].can_match[waiting_list[j]]) {

				are_matched = true;
				idx1 = waiting_list[i];
				idx2 = waiting_list[j];
				userlist[idx1].can_match[idx2] = false;
				userlist[idx2].can_match[idx1] = false;
				matchable_no = j > 1 ? j - 1 : 1;
			}
		}
	}
	if (are_matched) {
		int checked = 0;
		for (i = 0; i < waiting_num && checked < 2; i++) {
			if (idx1 == waiting_list[i]) {
				for (j = i; j < waiting_num; j++) {
					waiting_list[j] = waiting_list[j + 1];
				}
				waiting_num--;
				checked++;
				i = -1;
			}
			else if (idx2 == waiting_list[i]) {
				for (j = i; j < waiting_num; j++) {
					waiting_list[j] = waiting_list[j + 1];
				}
				waiting_num--;
				checked++;
				i = -1;
			}
		}

		booking(idx1, idx2);
	}
	else if (!are_matched){
		matchable_no = waiting_num;
	}
	return;
}


void initialize_userdata(int idx) {
	int i;
	int iSendResult;
	int recvbuflen = DEFAULT_BUFLEN;
	char sendbuf[DEFAULT_BUFLEN];
	mtx.lock();
	if (userlist[idx].waiting_now) {
		for (i = 0; waiting_list[i] != idx; i++);
		if (i <= matchable_no)
			matchable_no--;
		for (; waiting_list[i] != -1; i++) {
			waiting_list[i] = waiting_list[i + 1];
		}
		waiting_list[i] = -1;
		waiting_num--;
	}
	if (userlist[idx].chatting_now) {
		sendbuf[0] = SYS_DATA;
		memcpy(&sendbuf[1], &cons.length[RDC::DISCON_PARTNER], 4);
		memcpy(&sendbuf[5], &cons.sentence[RDC::DISCON_PARTNER], cons.length[RDC::DISCON_PARTNER]);
		iSendResult = send(userlist[userlist[idx].partner_idx].socket, sendbuf, cons.length[RDC::DISCON_PARTNER] + 5, 0);
		if (iSendResult == SOCKET_ERROR) {
			printf("send failed with error: %d\n", WSAGetLastError());
		}
		userlist[userlist[idx].partner_idx].chatting_now = false;
		userlist[userlist[idx].partner_idx].waiting_now = true;
		waiting_list[waiting_num] = userlist[idx].partner_idx;
		waiting_num++;
		if (waiting_num >= 2) {
			search_new_partner();
		}
	}
	for (i = 0; i < LIM_CONNECT; i++) {
		userlist[idx].can_match[i] = true;
		userlist[i].can_match[idx] = true;
	}
	userlist[idx].partner_idx = -1;
	userlist[idx].chatting_now = false;
	userlist[idx].passcode = -1;
	userlist[idx].partner_idx = -1;
	userlist[idx].waiting_now = false;
	userlist[idx].entering_now = false;
	userlist[idx].socket = INVALID_SOCKET;
	connected--;
	connect_list[connect_rear] = idx;
	connect_rear++;
	connect_rear %= LIM_CONNECT;
	mtx.unlock();
	return;
}

int send_partner(int idx, int length, char* recvbuf) { // 상대방에게 메시지를 전달하기 위한 함수.
	int iSendResult;
	char sendbuf[DEFAULT_BUFLEN];
	char temp[DEFAULT_BUFLEN];
	sendbuf[0] = CHAT_DATA;
	memcpy(&sendbuf[1], &length, 4);
	memcpy(&sendbuf[5], &recvbuf[0], length);
	iSendResult = send(userlist[userlist[idx].partner_idx].socket, sendbuf, length + 5, 0);
	if (iSendResult == SOCKET_ERROR) {
		return err_send(iSendResult);
	}
	return 0;

}

int recv_method(int idx, char* recv_buf, int length, int& errcheck) { // 데이터 수용 시 정상적 접근인지 확인하는 과정
	int iResult;
	iResult = recv(userlist[idx].socket, recv_buf, length, 0);

	if (iResult != length) {
		return 1;
	}
	if (errcheck == 0) {
		if ((userlist[idx].passcode^*(int*)recv_buf) != cons.code[CHECKCODE::START_CODE]) {
			return 2;
		}
	}
	else if (errcheck == 4) {
		if ((userlist[idx].passcode^*(int*)recv_buf) != cons.code[CHECKCODE::END_CODE]) {
			return 2;
		}
	}

	errcheck++;
	return 0;
}

void err_print(int idx, int err_no, int err_place) { // 각 에러 별 메시지.
	if (err_place == 4)
	{
		if (err_no == 2) {
			printf("Errer occures at endcode\n");
		}
		else
			printf("Errer occures at receving length\n");

		initialize_userdata(idx);
	}
	else if (err_place == 3) {
		printf("Errer occures at receving maintext\n");
		initialize_userdata(idx);
	}
	else if (err_place == 2) {
		printf("Errer occures at receving length\n");
		initialize_userdata(idx);
	}
	else if (err_place == 1) {
		printf("Errer occures at receving kinds\n");
		initialize_userdata(idx);
	}
	else if (err_place == 0) {
		if (err_no == 2) {
			printf("Errer occures at startcode\n");
		}
		else
			printf("Errer occures at connection\n");
		initialize_userdata(idx);
	}
}

void find_new_partner(int idx1, int idx2) {
	char sendbuf[DEFAULT_BUFLEN];
	int iSendResult;
	
	waiting_list[waiting_num] = idx1;
	waiting_num++;
	waiting_list[waiting_num] = idx2;
	waiting_num++;

	sendbuf[0] = SYS_DATA;
	memcpy(&sendbuf[1], &cons.length[RDC::DISCON_PARTNER], 4);
	memcpy(&sendbuf[5], &cons.sentence[RDC::DISCON_PARTNER], cons.length[RDC::DISCON_PARTNER]);
	iSendResult = send(userlist[idx1].socket, sendbuf, cons.length[RDC::DISCON_PARTNER] + 5, 0);
	if (iSendResult == SOCKET_ERROR) {
		printf("send failed with error: %d\n", WSAGetLastError());
	}
	iSendResult = send(userlist[idx2].socket, sendbuf, cons.length[RDC::DISCON_PARTNER] + 5, 0);
	if (iSendResult == SOCKET_ERROR) {
		printf("send failed with error: %d\n", WSAGetLastError());
	}
	userlist[idx1].chatting_now = false;
	userlist[idx1].waiting_now = true;
	userlist[idx2].chatting_now = false;
	userlist[idx2].waiting_now = true;
	userlist[idx1].partner_idx = -1;
	userlist[idx2].partner_idx = -1;
	if (waiting_num >= 2) {
		search_new_partner();
	}

}

int recv_thd(int idx) {
	int iResult, aResult, err_check, err_code;
	char check_len[5] = { 0, }, check_starts[5] = { 0, },
		check_kinds[2] = { 0, }, check_ends[5] = { 0, };
	char maintext[DEFAULT_BUFLEN] = { 0, };

	userlist[idx].entering->join();

	err_check = 5;
	while (err_check == 5) {
		err_check = 0;
		if (!(err_code = recv_method(idx, check_starts, 4, err_check))) { // start code 확인
			if (!(err_code = recv_method(idx, check_kinds, 1, err_check))) { // 전송된 데이터의 종류 확인
				if (!(err_code = recv_method(idx, check_len, 4, err_check))) { // 전송될 데이터의 길이 확인
					if (!(err_code = recv_method(idx, maintext, *(int*)check_len, err_check))) { // 메인 데이터 전송
						if (!(err_code = recv_method(idx, check_ends, 4, err_check))) {
							if (check_kinds[0] == CHAT_DATA && err_code == 0) {
								aResult = send_partner(idx, *(int*)check_len, maintext); // 상대방에게 메시지 전달
							}
							else if (check_kinds[0] == SYS_DATA && err_code == 0) {
								if (strcmp(maintext, cons.sentence[RDC::CON_CONFIRM]) == 0) {
									// 첫 접속시 대화할 준비가 되었음을 알리기 위한 코드
									// 밑의 코드를 통해 waiting_list에 queue로 들어간다.
									userlist[idx].waiting_now = true;
									mtx.lock();
									waiting_list[waiting_num] = idx;
									waiting_num++;
									if (waiting_num >= 2) {
										search_new_partner();
									}
									mtx.unlock();
								}
								else if (strcmp(maintext, cons.sentence[RDC::FIND_NEW_PARTNER]) == 0) {
									if (userlist[idx].waiting_now == false && userlist[idx].chatting_now) {
										mtx.lock();
										find_new_partner(idx, userlist[idx].partner_idx);
										mtx.unlock();
									}
				
								}
							}
						}
					}
				}
			}
		}
		if (err_check != 5)
			err_print(idx, err_code, err_check);
	}
	return 0;
}

int enter(int idx, int no1, int no2) { // client가 socket 생성후 가장 처음 가동되는 Thread.
	int iSendResult, iResult;
	char passcode[5];
	char recvbuf[DEFAULT_BUFLEN];
	char sendbuf[DEFAULT_BUFLEN];
	char ver_char[6];
	int recvbuflen = DEFAULT_BUFLEN;
	int temp;
	int length;
	int i;
	int ver = NEWEST_VER;
	bool getting_flag = true;

	memcpy(&ver_char, &ver, 4);
	iSendResult = send(userlist[idx].socket, ver_char , 4, 0);
	if (iSendResult == SOCKET_ERROR) {
		printf("send failed with error: %d\n", WSAGetLastError());
		initialize_userdata(idx);
		return 1;
	}

	memcpy(&passcode, &no1, 2);
	memcpy(&passcode[2], &no2, 2);
	memcpy(&userlist[idx].passcode, &passcode, 4);
	iSendResult = send(userlist[idx].socket, passcode, 4, 0);// 4바이트 무작위의 int값을 가장 처음 전달한다.
															 // 이 인트값은 고유값인 start값, end값과 xor연산되어 각 클라이언트에 저장되고, 
															 // 클라이언트는 정상적인 데이터 전송을 증명하기위해 데이터 패킷 양 끝에 변환된 start값과 end값을 삽입하여 정상적
															 // 데이터 교환임을 증명한다.
	if (iSendResult == SOCKET_ERROR) {
		printf("send failed with error: %d\n", WSAGetLastError());
		initialize_userdata(idx);
		return 1;
	}
	for (i = 0; i < LIM_CONNECT + 1; i++) {
		userlist[idx].can_match[i] = true;
	}
	userlist[idx].can_match[idx] = false;
	if (iSendResult == SOCKET_ERROR) {
		printf("send failed with error: %d\n", WSAGetLastError());
		initialize_userdata(idx);
		return 1;
	}
	sendbuf[0] = SYS_DATA;
	memcpy(&sendbuf[1], &cons.length[RDC::TURN_OFF], 4);
	memcpy(&sendbuf[5], &cons.sentence[RDC::TURN_OFF], cons.length[RDC::TURN_OFF]);
	iSendResult = send(userlist[idx].socket, sendbuf, 5 + cons.length[RDC::TURN_OFF], 0);
	if (iSendResult == SOCKET_ERROR) {
		printf("send failed with error: %d\n", WSAGetLastError());
		initialize_userdata(idx);
		return 1;
	}

	userlist[idx].recv = new std::thread(recv_thd, idx);

	return 0;
}

int main()
{
	int i;
	int iResult;
	int ver = NEWEST_VER;
	char ver_char[6];
	SOCKET ListenSocket = INVALID_SOCKET;
	SOCKET Temp_Socket = INVALID_SOCKET;
	int ran_no1, a;
	int ran_no2, b;
	int iSendResult;
	char crowded[4] = { 0,0,0,0 };
	int recvbuflen = DEFAULT_BUFLEN;
	std::thread *checking_running_info;
	
	seed = (int)times;
	srand(seed);
	for (i = 0; i <= 50; i++)
		rand();
	for (i = 0; i <= LIM_CONNECT; i++) {
		connect_list[i] = i;
		waiting_list[i] = -1;
	}
	
	checking_running_info = new std::thread(checking_info_server);

	cons.length[RDC::TURN_ON] = (int)strlen(cons.sentence[RDC::TURN_ON]);
	cons.length[RDC::TURN_OFF] = (int)strlen(cons.sentence[RDC::TURN_OFF]);
	cons.length[RDC::DISCON_PARTNER] = (int)strlen(cons.sentence[RDC::DISCON_PARTNER]);
	cons.length[RDC::CON_CONFIRM] = (int)strlen(cons.sentence[RDC::CON_CONFIRM]);
	cons.length[RDC::FIND_NEW_PARTNER] = (int)strlen(cons.sentence[RDC::FIND_NEW_PARTNER]);
	// 헤더에 저장된 코드들의 길이를 알기 위한 것.
	ListenSocket = initialize_listen();

	while (1) {
		// rand()함수는 0- 32768까지의 결과값만을 나타내어 보안코드로써는 부족하다고 판단.
		// rand() 함수를 4번 사용하여 1개의 32비트 랜덤수를 생성.
		ran_no1 = rand();
		ran_no2 = rand();
		while (ran_no1 == 0 && ran_no2 == 0) {
			ran_no1 = rand();
			ran_no2 = rand();
		}// 둘다 0 일시 이부분에서 걸러짐.
		a = rand();
		if (rand() % 2 == 0)
			ran_no1 = ran_no1 + 32768;
		b = rand();
		if (rand() % 2 == 0)
			ran_no2 = ran_no2 + 32768;
		iResult = listen(ListenSocket, SOMAXCONN);
		if (iResult == SOCKET_ERROR) {
			printf("listen failed with error: %d\n", WSAGetLastError());
		}

		// Accept a client socket
		Temp_Socket = accept(ListenSocket, NULL, NULL);
		if (Temp_Socket == INVALID_SOCKET) {
			printf("accept failed with error: %d\n", WSAGetLastError());
		}
		mtx.lock();
		if (LIM_CONNECT <= connected) {

			// 초과시 passcode로 0번을 보냄 (추가 진입 금지)
			// passcode는 0을 제외한 -2^31 ~ +2^31
			mtx.unlock();
			memcpy(&ver_char, &ver, 4);
			iSendResult = send(Temp_Socket, ver_char, 4, 0);
			if (iSendResult == SOCKET_ERROR) {
				printf("send failed with error: %d\n", WSAGetLastError());
			}
			iSendResult = send(Temp_Socket, crowded, 4, 0);
			if (iSendResult == SOCKET_ERROR) {
				printf("send failed with error: %d\n", WSAGetLastError());
			}
			continue;
		}
		connected++;
		userlist[connect_list[connect_top]].socket = Temp_Socket;
		int qi = sizeof(sock_name);
		//getsockname(Temp_Socket, (struct sockaddr *)&sock_name, &qi);
		//printf("%s", inet_ntoa(sock_name.sin_addr));
		Temp_Socket = INVALID_SOCKET;
		userlist[connect_list[connect_top]].entering = new std::thread(enter, connect_list[connect_top], ran_no1, ran_no2);
		connect_top++;
		connect_top %= LIM_CONNECT;
		mtx.unlock();
		printf("New client connected\n");
	}

	// shutdown the connection since we're done
	iResult = shutdown(Temp_Socket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed with error: %d\n", WSAGetLastError());
		closesocket(Temp_Socket);
		WSACleanup();
		return 1;
	}

	// cleanup
	closesocket(Temp_Socket);
	WSACleanup();
	return 0;
}
