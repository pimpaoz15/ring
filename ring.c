#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define max(A, B)((A) >= (B) ? (A) : (B));

#define maxlen 256 // tamanho maximo de uma string
#define Maxring 32 // tamanho maximo do anel (chaves de 0-31)
#define STDIN 0 // descritor relativo ao input do terminal

//struct declaration
struct servidor {
   // caracteristicas do nó
   int chave_propria; // contem a chave do próprio nó
   char porto_proprio[maxlen]; // contem o porto do próprio nó
   char ip_proprio[maxlen]; // contem o ip do próprio nó

   int chave_sucessor; // chave correspondente ao sucessor do nó
   int chave_predecessor; // chave correspondente ao predecessor do nó

   char porto_sucessor[maxlen]; // porto correspondente ao sucessor do nó
   char porto_predecessor[maxlen]; // porto correspondente ao predecessor do nó

   char ip_sucessor[maxlen]; // endereço ip correspondente ao sucessor do nó
   char ip_predecessor[maxlen]; // endereço ip correspondente ao predecessor do nó

   // sessoes tcp
   int fd_sucessor; // fd da sessao tcp estabelecida com o sucessor
   int fd_predecessor; // fd da sessao tcp estabelecida com o predecessor

   // sessão udp
   int fd_udp; // fd da sessão udp a ser estabelecida
   int chave_atalho;
   socklen_t addrlen_udp;
   ssize_t n_udp;
   struct sockaddr_in addr_udp;

   int max_fd;
};

//functions used in main.c declaration
void verifies_arguments(int argc, char * argv[]);
int read_arguments(int argc, char * argv[]);

//functions used in tcp
int inicializar_servidor_tcp(char port[maxlen]);
int aceitar_nova_conexao_tcp(int socket_servidor_tcp, struct sockaddr_in addr);
void tratar_conexao_tcp(int socket_cliente, struct servidor * server, fd_set * sockets_correntes);

//functions used in udp
int inicializar_servidor_udp(char port[maxlen]);
void aceitar_conexao_udp(int socket_servidor_udp, struct servidor * , fd_set * );

// functions used in order to create a client
int criar_cliente(char ip_destino[maxlen], char porto_destino[maxlen], int * max_fd, fd_set * rfds);

//functions used to create user command options
void processar_stdin(char buffer[maxlen], struct servidor * servidor, fd_set * sockets_correntes, int fd_socket_escuta_tcp, int socket_servidor_udp);
void new(struct servidor * node);
void pentry(int chave, char ip[maxlen], char porto[maxlen], struct servidor * node, fd_set * rfds);
void show(struct servidor * node);
void leave(struct servidor * node, fd_set * sockets_correntes);
int chord(char ip_destino[maxlen], char porto_destino[maxlen], int * max_fd, fd_set * sockets_correntes);
void echord(struct servidor * node, fd_set * sockets_correntes);
void find(int chave_a_procurar, struct servidor * node, int identificador, int chave_origem, char ip_origem[maxlen], char porto_origem[maxlen], fd_set * sockets_correntes);
void search(int chave_origem, struct servidor * node, int identificador, int chave_pretendida, char ip_pretendido[maxlen], char porto_pretendido[maxlen], fd_set * sockets_correntes);

//functions used to renew node information
void mensagem_recebida(char buffer[maxlen], struct servidor * node, int fd, fd_set * sockets_correntes);
void enviar_mensagem(int fd, char buffer[maxlen]);
void receber_mensagem(int socket, char buffer[maxlen], ssize_t * n);
void atualizar_predecessor(struct servidor * node, int chave, char ip[maxlen], char porto[maxlen]);
void atualizar_sucessor(struct servidor * node, int chave, char ip[maxlen], char porto[maxlen]);

int distancia_na_chave(int chave_procurada, int chave_propria);
int verificar(int fc, char msg[maxlen]);

int main(int argc, char * argv[]) {

   struct sockaddr_in addr_tcp;
   struct servidor node;
   int fd_socket_escuta, fd_socket_comunicaco, fd_socket_udp, i = 0;
   char buffer[maxlen];
   int counter;
   fd_set rfds, mascara; //descritor de leitura
   struct sigaction act;
   memset( & act, 0, sizeof act);
   act.sa_handler = SIG_IGN;

   if (sigaction(SIGPIPE, & act, NULL) == -1) exit(1); // sinal SIGPIPE será ignorado

   //variable initialization
   node.chave_predecessor = -1;
   node.chave_sucessor = -1;
   node.fd_sucessor = -1;
   node.fd_predecessor = -1;
   node.fd_udp = -1;
   node.chave_atalho = -1;

   sscanf(argv[1], "%d", & node.chave_propria); // vai buscar o valor da chave própria ao segundo argumento do terminal/input
   sscanf(argv[2], "%s", node.ip_proprio); // vai buscar o valor do ip ao terceiro elemento do terminal/input
   sscanf(argv[3], "%s", node.porto_proprio); // vai buscar o valor do porto ao quarto elemtno do terminal/input

   //printf("KEY: %d \nPORT: %s\nIP: %s\n", node.chave_propria, node.porto_proprio, node.ip_proprio);

   // inicializar servidor TCP e servidor UDP
   fd_socket_escuta = inicializar_servidor_tcp(node.porto_proprio);
   fd_socket_udp = inicializar_servidor_udp(node.porto_proprio);

   node.max_fd = fd_socket_escuta;
   node.max_fd = max(fd_socket_udp, node.max_fd);

   // inicialização dos file descriptors a zeros
   FD_ZERO( & rfds);
   FD_SET(STDIN, & rfds);
   FD_SET(fd_socket_escuta, & rfds);
   FD_SET(fd_socket_udp, & rfds);

   while (1) {

      mascara = rfds; // recorremos ao uso de 2 mascaras, porque assim não precisamos de estar sempre a limpar e só temos de dar
      // set ao descritor que se vai ligar

      counter = select((node.max_fd) + 1, & mascara, NULL, NULL, NULL); // função select (função destrutiva)
      if (counter <= 0) exit(1); // error

      if (FD_ISSET(STDIN, & mascara)) // são tratadas os fd_socket_escuta's relativos ao stdin
      {
         fgets(buffer, maxlen, stdin); // leitura do que foi escrito no terminal
         processar_stdin(buffer, & node, & rfds, fd_socket_escuta, fd_socket_udp); // processamento do comando introduzido
         //printf("Leu teclado\n");
      }

      for (i = 1; i < node.max_fd + 1; i++) // tratamento das restantes sockets (server ou talk)
      {

         if (FD_ISSET(i, & mascara)) {

            //printf("ISSEST com sucesso\n");
            if (i == fd_socket_escuta) // é feita uma chamada ao servidor, ou seja o socket do servidor precisa de ser lido
            {
               // o que implica que há uma nova conexão que precisa de ser aceite

               fd_socket_comunicaco = aceitar_nova_conexao_tcp(fd_socket_escuta, addr_tcp); // aceitamos a chamada ao servidor

               //printf("Alguem se conectou comigo!\n");
               // caso o socket do cliente seja maior que o fd_socket_comunicaçao, procedemos à atualização da variável
               node.max_fd = max(fd_socket_comunicaco, node.max_fd); // atualização do socket com o maior fd
               FD_SET(fd_socket_comunicaco, & rfds); // introdução do socket no set para ser analisado pelo select
            } // else if socket_servidor_tcp
            else if (i == fd_socket_udp) {
               aceitar_conexao_udp(fd_socket_udp, & node, & rfds);

            } else // caso o socket em estudo se trate uma chamada corrente tcp com uma conexão já existente então só temos de
            {
               // ler os dados do cliente e gerir a conexão
               // neste caso o i é o descritor da conexão tcp em causa
               //printf("Vou ver o que ele tem a dizer\n");
               tratar_conexao_tcp(i, & node, & rfds); // processamento do pretendido pelo cliente
            }

         }

      }

   }

   return 0;
}

/*-------------- inicializar_servidor_tcp ---------------
Does: inicializa o servidor tcp, responsável pela ligação
entre nós (apenas à volta)
Recieves:
Returns: fd_socket_escuta
Side-Effects:
-------------------------------------------------------*/
int inicializar_servidor_tcp(char port[maxlen]) {
   struct addrinfo * res;
   int fd_socket_escuta = 0;
   struct addrinfo hints;

   // criação da socket TCP
   if ((fd_socket_escuta = socket(AF_INET, SOCK_STREAM, 0)) == -1)
      exit(1); //error

   // estrutura do endereço
   memset( & hints, 0, sizeof hints);
   hints.ai_family = AF_INET; // IPv4
   hints.ai_socktype = SOCK_STREAM; // SOCKET TCP
   hints.ai_flags = AI_PASSIVE; // servidor

   if (getaddrinfo(NULL, port, & hints, & res) != 0) {

      printf("Falha ao obter endereço do servidor");
      exit(1);
   } // if

   // associação do socket criado ao porto onde queremos que o servidor esteja
   if (bind(fd_socket_escuta, res -> ai_addr, res -> ai_addrlen) == -1)
      exit(1); //error

   if (listen(fd_socket_escuta, 5) == -1)
      exit(1); //error

   return fd_socket_escuta;
}

/*-------------- aceitar_nova_conexao_tcp ---------------
Does: inicializa o servidor tcp, responsável pela ligação
entre nós (apenas à volta)
Recieves:
Returns: newfd
Side-Effects:
-------------------------------------------------------*/

int aceitar_nova_conexao_tcp(int fd, struct sockaddr_in addr) {

   socklen_t addrlen;
   int newfd;
   addrlen = sizeof(addr);

   if ((newfd = accept(fd, (struct sockaddr * ) & addr, & addrlen)) == -1) /*error*/ exit(1);

   // Aceitamos uma nova conexão tcp e atribuimos um socket novo (e único, claro)
   return newfd;
}

/*----------------- tratar_conexao_tcp ------------------
Does: inicializa o servidor tcp, responsável pela ligação
entre nós (apenas à volta)
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void tratar_conexao_tcp(int socket_comunicacao, struct servidor * node, fd_set * sockets_correntes) {

   char buffer[maxlen];
   ssize_t n;

   receber_mensagem(socket_comunicacao, buffer, & n);
   
   //caso se perca a ligacao com um dos nos
   if (n == 0) {
    

      

      // no caso de se perder a conexão com o sucessor, e o leave com mais de 2 nós
      if (socket_comunicacao == ( * node).fd_sucessor) {
         ( * node).fd_sucessor = -2;
         FD_CLR(socket_comunicacao, sockets_correntes);
         close(socket_comunicacao);
      }
      // no caso de estarem dois nós no anel e ele vá ficar sozinho
      if ((node -> chave_predecessor == node -> chave_sucessor) && (socket_comunicacao == node -> fd_predecessor)) {

         FD_CLR(socket_comunicacao, sockets_correntes);
         close(socket_comunicacao);
         new(node);
         return;

      }

      // retiramos do "ring" a ligação TCP perdida
      return;
   }

   mensagem_recebida(buffer, node, socket_comunicacao, sockets_correntes); //processa-se a mensagem recebida

   return;
}

/*----------------- mensagem_recebida ------------------
Does: inicializa o servidor tcp, responsável pela ligação
entre nós (apenas à volta)
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void mensagem_recebida(char buffer[maxlen], struct servidor * node, int fd, fd_set * sockets_correntes) {
   int chave = 0, temp_fd = 0, v, identificador, chave_origem, chave_pretendida;
   char comando[maxlen], ip[maxlen], porto[maxlen], resposta[maxlen], temp_chave[maxlen], ip_origem[maxlen], porto_origem[maxlen];
   sscanf(buffer, "%s %s %s %s %s %s\n", comando, temp_chave, ip, porto, ip_origem, porto_origem);

   // comando recebido é SELF
   if (!strcmp(comando, "SELF")) {
      sscanf(temp_chave, "%d", & chave);
      //printf("Recebi um SELF e o meu socket de comunicacao ao sucessor e %d\n", node -> fd_sucessor);
      //printf("%d\n",(*node).fd_sucessor);
      v = -2;

      //anel que se quer juntar recebe SELF
      if (v == node -> fd_sucessor) {

         
         node -> fd_sucessor = fd;
         atualizar_sucessor(node, chave, ip, porto);

      } //if
      else {
        
         // no caso de existir apenas um nó no ring e receber SELF
         if ((( * node).fd_sucessor == -1) && (( * node).fd_predecessor == -1))

         {
            printf("Sou um no sozinho no anel\n");
            
           
            ( * node).fd_sucessor = fd;
            atualizar_sucessor(node, chave, ip, porto);

            // estabelecimento de uma ligação com o nosso predecessor
            ( * node).fd_predecessor = criar_cliente(ip, porto, & ( * node).max_fd, sockets_correntes);
            // como o nó está sozinho no ring, o nó que entra vai ser também o predecessor
            atualizar_predecessor(node, chave, ip, porto);
          
            sprintf(resposta, "SELF %d %s %s\n", ( * node).chave_propria, ( * node).ip_proprio, ( * node).porto_proprio);
           
            enviar_mensagem(( * node).fd_predecessor, resposta);

         } // if

         // no caso do ring já ter pelo menos 2 nós
         else {
           

            atualizar_sucessor(node, chave, ip, porto);
            temp_fd = node -> fd_sucessor;
            //node->fd_sucessor=-2; //calma, já vamos dizer qual é (nó entrante)

            sprintf(resposta, "PRED %d %s %s\n", chave, ip, porto);
            enviar_mensagem(temp_fd, resposta);

            ( * node).fd_sucessor = fd;

            close(temp_fd);
            FD_CLR(temp_fd, & ( * sockets_correntes));
           

         }
      }
      return;
   }

   if (!strcmp(comando, "PRED")) {
      printf("Estou a fazer o pred\n");
      sscanf(temp_chave, "%d", & chave);
      close(node -> fd_predecessor);
      FD_CLR(node -> fd_predecessor, & ( * sockets_correntes));

      ( * node).fd_predecessor = criar_cliente(ip, porto, & ( * node).max_fd, sockets_correntes);
      atualizar_predecessor(node, chave, ip, porto);
      sprintf(resposta, "SELF %d %s %s\n", ( * node).chave_propria, ( * node).ip_proprio, ( * node).porto_proprio);
      enviar_mensagem(( * node).fd_predecessor, resposta);

      return;
   }

   if (!strcmp(comando, "FND")) {
      sscanf(temp_chave, "%d", & chave);
      sscanf(ip, "%d", & identificador);
      sscanf(porto, "%d", & chave_origem);

      find(chave, node, identificador, chave_origem, ip_origem, porto_origem, sockets_correntes);

      return;
   }

   if (!strcmp(comando, "RSP")) {
      sscanf(temp_chave, "%d", & chave);
      sscanf(ip, "%d", & identificador);
      sscanf(porto, "%d", & chave_pretendida);

      //ip_origem=ip_pretendido e porto_origem_=porto_pretendido
      search(chave, node, identificador, chave_pretendida, ip_origem, porto_origem, sockets_correntes);

      return;
   }

   return;
}

/*------------------ processar_stdin --------------------
Does: read terminal input and proceeds to call the right
function in order to execute the order
Recieves: argc, argv
Returns: int
Side-Effects: exits if args not ok
-------------------------------------------------------*/

void processar_stdin(char buffer[maxlen], struct servidor * node, fd_set * rfds, int fd_socket_escuta_tcp, int fd_socket_udp) {

   char comando[maxlen], ip[maxlen], porto[maxlen];
   int chave = 0;

   // separar a mensagem introduzida por partes, estabelecendo parametros novos
   sscanf(buffer, "%s %d %s %s\n", comando, & chave, ip, porto);

   if (!strcmp(comando, "n")) { // new
      printf("Estou a fazer um new\n");
      if (node -> chave_propria != -1) {
         new(node);
      }
      return;

   } else if (!strcmp(comando, "p")) { // pentry

      printf("Estou a fazer o pentry\n");

      pentry(chave, ip, porto, node, rfds);

      return;

   } else if (!strcmp(comando, "l")) { // leave

      printf("Estou a fazer o leave\n");

      leave(node, rfds);

      return;
   } else if (!strcmp(comando, "s")) { // show

      printf("Estou a fazer o show\n");
      show(node);
      return;
   } else if (!strcmp(comando, "c")) { // chord
      node -> fd_udp = chord(ip, porto, & ( * node).max_fd, rfds);
      node -> chave_atalho = chave;
      return;
   } else if (!strcmp(comando, "ec")) { // echord
      echord(node, rfds);

   } else if (!strcmp(comando, "f")) { // find

      find(chave, node, 0, node -> chave_propria, node -> ip_proprio, node -> porto_proprio, rfds);

      return;
   } else if (!strcmp(comando, "e")) { // exit

      // fazer o mesmo que o comando "leave"
      leave(node, rfds);

      // fechar os sockets e fechar o programa
      FD_CLR(fd_socket_escuta_tcp, & ( * rfds));
      close(fd_socket_escuta_tcp);
      exit(0);
   }

   return;
}

/*------------------------ new --------------------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void new(struct servidor * node) {

   // inicializar sockets
   ( * node).fd_predecessor = -1;
   ( * node).fd_sucessor = -1;
   // inicializar chaves

   ( * node).chave_sucessor = ( * node).chave_propria;
   ( * node).chave_predecessor = ( * node).chave_propria;
   // inicializar porto e ip
   strcpy(( * node).porto_sucessor, ( * node).porto_proprio);
   strcpy(( * node).porto_predecessor, ( * node).porto_proprio);
   strcpy(( * node).ip_sucessor, ( * node).ip_proprio);
   strcpy(( * node).ip_predecessor, ( * node).ip_proprio);

   return;
}

/*----------------------- pentry -----------------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void pentry(int chave, char ip[maxlen], char porto[maxlen], struct servidor * node, fd_set * rfds) {

   char buffer[maxlen];

   // criamos no nó que entra uma ligação TCP com o seu futuro predecessor

   atualizar_predecessor(node, chave, ip, porto);

   // criação de talk socket
   ( * node).fd_sucessor = -2; 
   ( * node).fd_predecessor = criar_cliente(ip, porto, & ( * node).max_fd, rfds);

   // criação da resposta com comando SELF e envio para o seu predecessor
   sprintf(buffer, "SELF %d %s %s\n", ( * node).chave_propria, ( * node).ip_proprio, ( * node).porto_proprio);

   enviar_mensagem(( * node).fd_predecessor, buffer);

   return;
}

/*----------------- enviar_mensagem --------------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void enviar_mensagem(int fd, char buffer[maxlen]) {
   ssize_t nleft = 0, nwritten;
   char * ptr;

   ptr = buffer;
   nleft = strlen(buffer);

   while (nleft > 0) {
      nwritten = write(fd, ptr, nleft);
      if (nwritten <= 0) /*error*/ exit(1);
      nleft -= nwritten;
      ptr += nwritten;
   }
   printf("Mensagem enviada: %s\n", buffer);
   return;
}

/*------------------ receber_mensagem -------------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void receber_mensagem(int socket, char buffer[maxlen], ssize_t * n) {
   char * ptr;
   int i = 0, temp = 0;
   ptr = buffer;

   printf("Vou ler a mensagem\n");
   do {
      // ler o caractér recebido, guardando-o na posição apontada por ptr
      verificar( * n = read(socket, ptr, 1), "Erro na leitura!\n");

      // detetar o fim da string que foi recebida
      if ( * ptr == '\n') {
         temp = 1;

         // confirmar o fim da string depois de recebermos o '\n'
         buffer[i + 1] = '\0';
      } else {
         // fazemos avançar o ponteiro ao longo da string recebida
         ptr += 1;
         i++;
      }
   }
   while (temp == 0);
   printf("Mensagem recebida: %s\n", buffer);
   return;
}

/*--------------- atualizar_predecessor ----------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void atualizar_predecessor(struct servidor * node, int chave, char ip[maxlen], char porto[maxlen]) {

   ( * node).chave_predecessor = chave;
   strcpy(( * node).ip_predecessor, ip);
   strcpy(( * node).porto_predecessor, porto);
   return;
}

/*---------------- atualizar_sucecessor -----------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void atualizar_sucessor(struct servidor * node, int chave, char ip[maxlen], char porto[maxlen]) {

   ( * node).chave_sucessor = chave;
   strcpy(( * node).ip_sucessor, ip);
   strcpy(( * node).porto_sucessor, porto);
   return;
}

/*------------------- criar_cliente ---------------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

int criar_cliente(char ip_destino[maxlen], char porto_destino[maxlen], int * max_fd, fd_set * rfds) {
   int fd, n;
   struct addrinfo hints, * res;

   fd = socket(AF_INET, SOCK_STREAM, 0); //socket TCP
   if (fd == -1) exit(1); //error

   memset( & hints, 0, sizeof hints);
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;

   n = getaddrinfo(ip_destino, porto_destino, & hints, & res);
   if (n != 0) /*error*/ exit(1);

   n = connect(fd, res -> ai_addr, res -> ai_addrlen);
   if (n == -1) /*error*/ exit(1);

   * max_fd = max(fd, * max_fd); // atualizar valor do maior file descriptor registado
   FD_SET(fd, & ( * rfds)); // talk_socket adicionada ao set de sockets a ser considerado pelo select

   return fd; // retornamos o fd relativamente ao talk_socket criado
}

/*---------------------- leave -------------------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void leave(struct servidor * node, fd_set * mascara) {
   // retiram-se dos sockets TCP em funcionamento para serem considerados pelo select() no main ()
   char mensagem[maxlen];

   sprintf(mensagem, "PRED %d %s %s\n", node -> chave_predecessor, node -> ip_predecessor, node -> porto_predecessor);
   enviar_mensagem(node -> fd_sucessor, mensagem);

   FD_CLR((node) -> fd_sucessor, & ( * mascara));
   FD_CLR(( * node).fd_predecessor, & ( * mascara));
   // fecho dos sockets TCP (atualmente "abertos")
   close(( * node).fd_predecessor);
   close(( * node).fd_sucessor);
   // reset dos FD TCp com sucessor e predecessor
   ( * node).fd_predecessor = -1;
   ( * node).fd_sucessor = -1;
   node -> chave_predecessor = -1;
   node -> chave_sucessor = -1;
   return;

   /*---------------------- show -------------------------
   Does: sh
   Recieves:
   Returns:
   Side-Effects:
   -------------------------------------------------------*/

}
void show(struct servidor * node) {
   printf("No em causa: chave %d, ip %s, porto %s \n", ( * node).chave_propria, ( * node).ip_proprio, ( * node).porto_proprio);
   printf("O seu sucessor: chave %d, ip %s, porto %s\n", ( * node).chave_sucessor, ( * node).ip_sucessor, ( * node).porto_sucessor);
   printf("O seu predecessor: chave %d, ip %s, porto %s \n", ( * node).chave_predecessor, ( * node).ip_predecessor, ( * node).porto_predecessor);
   printf("O meu descritor com o succ é %d e com o pred é %d", ( * node).fd_sucessor, node -> fd_predecessor);
   printf("\n");
   return;
}

int verificar(int fc, char msg[maxlen]) {
   if (fc == -1) {
      printf("%s", msg);
      exit(1);
   }
   return fc;
}

/*------------- inicializar_servidor_udp ---------------
Does: inicializa o servidor udp, resonsável pela criação
de atalhos entre nóis
Recieves: porto
Returns: socket do servidor udp
Side-Effects:
-------------------------------------------------------*/

int inicializar_servidor_udp(char port[maxlen]) {
   int socket_servidor_udp = 0;
   struct addrinfo hints, * res;

   // criação do socket UDP
   verificar((socket_servidor_udp = socket(AF_INET, SOCK_DGRAM, 0)),
      "Falha ao criar socket");

   // estrutura do endereço
   memset( & hints, 0, sizeof hints);
   hints.ai_family = AF_INET; // IPv4
   hints.ai_socktype = SOCK_DGRAM; // SOCKET UDP
   hints.ai_flags = AI_PASSIVE; // servidor

   if (getaddrinfo(NULL, port, & hints, & res) != 0) {

      printf("Falha ao obter endereço do servidor");
      exit(1);
   }

   //
   // associação do socket criado ao porto onde queremos que o servidor esteja
   verificar(bind(socket_servidor_udp, res -> ai_addr, res -> ai_addrlen),
      "Bind Falhou");

   return socket_servidor_udp;
}

/*------------- aceitar_conexao_udp ---------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void aceitar_conexao_udp(int socket_servidor_udp, struct servidor * node, fd_set * sockets_correntes) {
   int chave_procurada = 0;
   char buffer[maxlen], comando[maxlen];
   ( * node).addrlen_udp = sizeof(( * node).addr_udp);

   // leitura da mensagem recebida pela sessão UDP e atribuição dos respetivos valores às variáveis
   verificar(( * node).n_udp = recvfrom(socket_servidor_udp, buffer, maxlen, 0, (struct sockaddr * ) & (( * node).addr_udp), & (( * node).addrlen_udp)),
      "Erro no Recvfrom!\n");
   sscanf(buffer, "%s %d", comando, & chave_procurada);

   return;
}

/*---------------------- chord -------------------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

int chord(char ip_destino[maxlen], char porto_destino[maxlen], int * max_fd, fd_set * sockets_correntes) {
   int fd, errcode;
   ssize_t n;
   struct addrinfo hints, * res;

   verificar(fd = socket(AF_INET, SOCK_STREAM, 0),
      "Erro na criação da talk_socket|\n"); // socket TCP é criado

   memset( & hints, 0, sizeof hints);
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;

   errcode = getaddrinfo(ip_destino, porto_destino, & hints, & res); // informacao do endereço destino para podermos dar connect
   if (errcode != 0) {
      printf("Erro na obtenção da informação do endereço!\n");
      exit(1);
   }
   verificar(n = connect(fd, res -> ai_addr, res -> ai_addrlen),
      "Erro na conexão com o servidor!\n"); // ligamos-nos ao socket do servidor destino

   * max_fd = max(fd, * max_fd); // atualização do maior file descriptor (entre os dois, fd e max_fd qual o maior)

   FD_SET(fd, & ( * sockets_correntes)); // chord adicionada ao set de sockets que são considerados pelo select
   return fd; //retorno do fd relativo à chord que foi criada
}

/*---------------------- echord -------------------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void echord(struct servidor * node, fd_set * mascara) {

   FD_CLR((node) -> fd_udp, & ( * mascara));
   // fecho dos sockets UDP (atualmente "abertos")
   close(( * node).fd_udp);
   // reset dos FD UDP
   ( * node).fd_udp = -1;
   return;
}

/*---------------------- find -------------------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void find(int chave_a_procurar, struct servidor * node, int identificador, int chave_origem, char ip_origem[maxlen], char porto_origem[maxlen], fd_set * sockets_correntes)

{
   int distancia_TCP_propria, distancia_TCP_sucessor, distancia_UDP;
   char resposta[maxlen];
   ssize_t n;

   printf("Chave a procurar %d e a chave de origem e a %d\n", chave_a_procurar, chave_origem);

   distancia_TCP_sucessor = distancia_na_chave(chave_a_procurar, ( * node).chave_sucessor);
   printf("distancia do meu sucessor ao pretendido %d\n", distancia_TCP_sucessor);
   distancia_TCP_propria = distancia_na_chave(chave_a_procurar, ( * node).chave_propria);
   printf("distancia de mim ao pretendido %d\n", distancia_TCP_propria);

   // se a distância da chave sucessora face à chave procurada for menor do que a distância
   // da chave procurada à própria chave, então deve-se reencaminhar o comando FND para o sucessor 
   if (distancia_TCP_sucessor < distancia_TCP_propria) {

      if (node -> fd_udp == -1) {

         sprintf(resposta, "FND %d %d %d %s %s\n", chave_a_procurar, identificador, chave_origem, ip_origem, porto_origem);
         enviar_mensagem(( * node).fd_sucessor, resposta);

      } else {

         distancia_UDP = distancia_na_chave(chave_a_procurar, ( * node).chave_atalho);
         //se for mais pero por UDP
         if (distancia_TCP_sucessor > distancia_UDP) {

            sprintf(resposta, "FND %d %d %d %s %s", chave_a_procurar, identificador, chave_origem, ip_origem, porto_origem);
            verificar(n = sendto(( * node).fd_udp, resposta, maxlen, 0, (struct sockaddr * ) & (( * node).addr_udp), ( * node).addrlen_udp),
               "Erro no envio da resposta pela sessão UDP criada");

            //Se for mais perto por TCP
         } else {

            sprintf(resposta, "FND %d %d %d %s %s\n", chave_a_procurar, identificador, chave_origem, ip_origem, porto_origem);
            enviar_mensagem(( * node).fd_sucessor, resposta);

         }
      }
   } // if

   // Enviar resposta ao no origem
   else {
      printf("A chave a procurar esta em mim, tenho de mandar resposta\n");

      search(chave_origem, node, identificador, node -> chave_propria, node -> ip_proprio, node -> porto_proprio, sockets_correntes);

   } // else

   return;
}

/*----------------- distancia_na_chave -----------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

int distancia_na_chave(int chave_procurada, int chave_propria) {
   int distancia = 0;
   if ((chave_procurada - chave_propria) < 0) // se a distancia for negativa, ha que somar o tamanho
      // máximo do anel
      distancia = chave_procurada - chave_propria + Maxring;
   else // caso contrário, então a distância será dada por
      distancia = chave_procurada - chave_propria;
   return distancia;
}

/*---------------------- search -------------------------
Does:
Recieves:
Returns:
Side-Effects:
-------------------------------------------------------*/

void search(int chave_origem, struct servidor * node, int identificador, int chave_pretendida, char ip_pretendido[maxlen], char porto_pretendido[maxlen], fd_set * sockets_correntes) {

   int distancia_TCP_propria, distancia_TCP_sucessor, distancia_UDP;
   char resposta[maxlen];
   ssize_t n;

   printf("Chave que mandou procurar e a chave %d\n", chave_origem);

   distancia_TCP_sucessor = distancia_na_chave(chave_origem, ( * node).chave_sucessor);
   printf("distancia do meu sucessor ao pretendido %d\n", distancia_TCP_sucessor);
   distancia_TCP_propria = distancia_na_chave(chave_origem, ( * node).chave_propria);
   printf("distancia de mim ao pretendido %d\n", distancia_TCP_propria);

   // se a distância da chave sucessora face à chave procurada for menor do que a distância
   // da chave procurada à própria chave, então deve-se reencaminhar o comando FND para o sucessor 
   if (distancia_TCP_sucessor < distancia_TCP_propria) {

      if (node -> fd_udp == -1) {

         sprintf(resposta, "RSP %d %d %d %s %s\n", chave_origem, identificador, chave_pretendida, ip_pretendido, porto_pretendido);
         enviar_mensagem(( * node).fd_sucessor, resposta);

      } else {
         distancia_UDP = distancia_na_chave(chave_origem, ( * node).chave_atalho);
         //se for mais pero por UDP
         if (distancia_TCP_sucessor > distancia_UDP) {

            sprintf(resposta, "RSP %d %d %d %s %s", chave_origem, identificador, chave_pretendida, ip_pretendido, porto_pretendido);
            verificar(n = sendto(( * node).fd_udp, resposta, maxlen, 0, (struct sockaddr * ) & (( * node).addr_udp), ( * node).addrlen_udp),
               "Erro no envio da resposta pela sessão UDP criada");

            //Se for mais perto por TCP
         } else {

            sprintf(resposta, "RSP %d %d %d %s %s\n", chave_origem, identificador, chave_pretendida, ip_pretendido, porto_pretendido);
            enviar_mensagem(( * node).fd_sucessor, resposta);

         }
      }
   } // if

   // Enviar resposta ao no origem
   else {
     // printf("Recebi a resposta. Já tenho o IP: %s e porto: %s do gajo a quem me quero juntar\n", ip_pretendido, porto_pretendido);

   } // else

   return;
}
