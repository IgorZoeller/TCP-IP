#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define PORT "3490" // the port users will be connecting to
#define BACKLOG 1 // how many pending connections queue will hold
#define MAXDATASIZE 256 // max number of bytes we can get at once 

typedef unsigned char BYTE;

bool testaBytes(BYTE *buf, BYTE b, int n) {
    //Testa se n bytes da memoria buf possuem valor b
    bool igual = true;
    for (unsigned i = 0; i < n; i++)
        if (buf[i] != b)
        {
            igual = false;
            break;
        }
    return igual;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

class SERVER {
    private:

    int sockfd, new_fd; // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    public:
    SERVER();
    ~SERVER();

    void waitConnection();
    void sendBytes(int nBytesToSend, BYTE *buf);
    void receiveBytes(int nBytesToReceive, BYTE *buf);

};

SERVER::SERVER() {
    struct addrinfo *servinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }
    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1)
        {
            perror("server: socket");
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo); // all done with this structure
    if (p == NULL)
    {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }
    if (listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }
}

SERVER::~SERVER() {
    close(new_fd);
}

void SERVER::waitConnection() {
    while (1)
    {
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1)
        {
            perror("accept");
            continue;
        }
        else
            break;
    }

    inet_ntop(their_addr.ss_family,
              get_in_addr((struct sockaddr *)&their_addr),
              s, sizeof s);
    printf("server: got connection from %s\n", s);
    close(sockfd);
}

void SERVER::sendBytes(int nBytesToSend, BYTE *buf) {
    int sentBytes = 0, currentSentBytes;
    while(sentBytes <= nBytesToSend) {
        BYTE *sendBuf = &buf[sentBytes];
        currentSentBytes = send(new_fd, sendBuf, nBytesToSend-sentBytes, 0);
        if (currentSentBytes == -1) perror("send");
        else sentBytes += currentSentBytes;
    }
}

void SERVER::sendUint(uint32_t m) {
    uint32_t m_s = htonl(m);
    int nBytesToSend = 4;
    BYTE buf[4];
    buf[0] = (m_s >> 24) & 0xFF;
    buf[1] = (m_s >> 16) & 0xFF;
    buf[2] = (m_s >> 8) & 0xFF;
    buf[3] = m_s & 0xFF;
    int sentBytes = 0, currentSentBytes;
    while (sentBytes < nBytesToSend)
    {
        BYTE *subbuf = &buf[sentBytes];
        currentSentBytes = send(new_fd, subbuf, nBytesToSend - sentBytes, 0);
        if (currentSentBytes == -1)
            perror("send");
        else
        {
            sentBytes += currentSentBytes;
        }
    }
}

void SERVER::sendVb(const vector<BYTE> &vb) {
    vector<BYTE> st;
    st.clear();
    copy(vb.begin(), vb.end(), back_inserter(st));
    sendUint(vb.size()); // Enviando um header com o tamanho da mensagem.
    sendBytes(vb.size(),st.data());
}

void SERVER::receiveBytes(int nBytesToReceive, BYTE *buf) {
    int receivedBytes = 0, currentReceivedBytes;
    while(receivedBytes <= nBytesToReceive) {
        BYTE recvBuf[nBytesToReceive - receivedBytes];
        currentReceivedBytes = recv(new_fd, recvBuf, nBytesToReceive - receivedBytes, 0);
        if (currentReceivedBytes == -1) perror("recv");
        else {
            for (int i = receivedBytes; i < receivedBytes + currentReceivedBytes; i++) {
                buf[i] = recvBuf[i - receivedBytes];
            }
            receivedBytes += currentReceivedBytes;
        }
    }
}

void SERVER::receiveUint(uint32_t &m) {
    int nBytesToReceive = 4;
    BYTE buf[4];
    int receivedBytes = 0, currentReceivedBytes = 0;
    while (receivedBytes < nBytesToReceive)
    {
        BYTE subbuf[nBytesToReceive - receivedBytes];
        currentReceivedBytes = recv(new_fd, subbuf, nBytesToReceive - receivedBytes, 0);
        if (currentReceivedBytes == -1)
            perror("recv");
        else
        {
            for (int i = receivedBytes; i < receivedBytes + currentReceivedBytes; i++)
                buf[i] = subbuf[i - receivedBytes];
            receivedBytes += currentReceivedBytes;
        }
    }
    m = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
    m = ntohl(m);
}

void SERVER::receiveVb(vector<BYTE> &st) {
    st.clear();
    uint32_t t;
    receiveUint(t); // Leitura do header para saber o tamanho da mensagem
    vector<BYTE> vb;
    vb.resize(t);
    receiveBytes(t,vb.data());
    copy(vb.begin(),vb.end(),back_inserter(st));
}

class CLIENT {

    private:

    int sockfd, numbytes;
    struct addrinfo hints, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    public:
    
    CLIENT(string address);
    ~CLIENT();

    void waitConnection();
    void sendBytes(int nBytesToSend, BYTE *buf);
    void receiveBytes(int nBytesToReceive, BYTE *buf);

};

CLIENT::CLIENT(string address) {
    struct addrinfo *servinfo;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if ((rv = getaddrinfo(address.c_str(), PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
            p->ai_protocol)) == -1) {
        perror("client: socket");
        continue;
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
        perror("client: connect");
        close(sockfd);
        continue;
        }
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
        s, sizeof s);
    printf("client: connecting to %s\n", s);
    freeaddrinfo(servinfo); // all done with this structure
}

CLIENT::~CLIENT() {
    close(sockfd);
}

void CLIENT::sendBytes(int nBytesToSend, BYTE *buf) {
    int sentBytes = 0, currentSentBytes;
    while(sentBytes <= nBytesToSend) {
        BYTE *sendBuf = &buf[sentBytes];
        currentSentBytes = send(sockfd, sendBuf, nBytesToSend-sentBytes, 0);
        if (currentSentBytes == -1) perror("send");
        else sentBytes += currentSentBytes;
    }
}

void CLIENT::sendUint(uint32_t m) {
    uint32_t m_s = htonl(m);
    int nBytesToSend = 4;
    BYTE buf[4];
    buf[0] = (m_s >> 24) & 0xFF;
    buf[1] = (m_s >> 16) & 0xFF;
    buf[2] = (m_s >> 8) & 0xFF;
    buf[3] = m_s & 0xFF;
    int sentBytes = 0, currentSentBytes = 0;
    while (sentBytes < nBytesToSend)
    {
        BYTE *subbuf = &buf[sentBytes];
        currentSentBytes = send(sockfd, subbuf, nBytesToSend - sentBytes, 0);
        if (currentSentBytes == -1)
            perror("send");
        else
        {
            sentBytes += currentSentBytes;
        }
    }
}

void CLIENT::sendVb(const vector<BYTE> &vb) {
    vector<BYTE> st;
    st.clear();
    copy(vb.begin(), vb.end(), back_inserter(st));
    sendUint(vb.size()); // Enviando header com tamanho da mensagem
    sendBytes(vb.size(), st.data());
}

void CLIENT::receiveBytes(int nBytesToReceive, BYTE *buf) {
    int receivedBytes = 0, currentReceivedBytes;
    while(receivedBytes <= nBytesToReceive) {
        BYTE recvBuf[nBytesToReceive - receivedBytes];
        currentReceivedBytes = recv(sockfd, recvBuf, nBytesToReceive - receivedBytes, 0);
        if (currentReceivedBytes == -1) perror("recv");
        else {
            for (int i = receivedBytes; i < receivedBytes + currentReceivedBytes; i++) {
                buf[i] = recvBuf[i - receivedBytes];
            }
            receivedBytes += currentReceivedBytes;
        }
    }
}

void CLIENT::receiveUint(uint32_t &m) {
    int nBytesToReceive = 4;
    BYTE buf[4];
    int receivedBytes = 0, currentReceivedBytes = 0;
    while (receivedBytes < nBytesToReceive)
    {
        BYTE subbuf[nBytesToReceive - receivedBytes];
        currentReceivedBytes = recv(sockfd, subbuf, nBytesToReceive - receivedBytes, 0);
        if (currentReceivedBytes == -1)
            perror("recv");
        else
        {
            for (int i = receivedBytes; i < receivedBytes + currentReceivedBytes; i++)
                buf[i] = subbuf[i - receivedBytes];
            receivedBytes += currentReceivedBytes;
        }
    }
    m = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
    m = ntohl(m);
}

void CLIENT::receiveVb(vector<BYTE> &st) {
    st.clear();
    uint32_t t;
    receiveUint(t); // Leitura do header com o tamanho da mensagem.
    vector<BYTE> vb;
    vb.resize(t);
    receiveBytes(t, vb.data());
    copy(vb.begin(), vb.end(), back_inserter(st));
}