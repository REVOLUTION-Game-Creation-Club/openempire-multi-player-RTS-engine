#include "Sockets.h"

#include "Packet.h"
#include "Util.h"

#include <stdlib.h>
#include <stdbool.h>

Sockets Sockets_Init(void)
{
    static Sockets zero;
    Sockets sockets = zero;
    sockets.set = SDLNet_AllocSocketSet(COLOR_COUNT);
    return sockets;
}

void Sockets_Free(const Sockets sockets)
{
    SDLNet_FreeSocketSet(sockets.set);
}

Sockets Sockets_Add(Sockets sockets, TCPsocket sock)
{
    for(int32_t i = 0; i < COLOR_COUNT; i++)
        if(sockets.sock[i] == NULL)
        {
            SDLNet_TCP_AddSocket(sockets.set, sock);
            sockets.sock[i] = sock;
            return sockets;
        }
    return sockets;
}

Sockets Sockets_Service(Sockets sockets, const int32_t timeout)
{
    if(SDLNet_CheckSockets(sockets.set, timeout))
        for(int32_t i = 0; i < COLOR_COUNT; i++)
        {
            TCPsocket sock = sockets.sock[i];
            if(SDLNet_SocketReady(sock))
            {
                static Overview zero;
                Overview overview = zero;
                const int32_t max = sizeof(overview);
                const int32_t bytes = SDLNet_TCP_Recv(sock, &overview, max);
                if(bytes <= 0)
                {
                    SDLNet_TCP_DelSocket(sockets.set, sock);
                    sockets.sock[i] = NULL;
                }
                if(bytes == max)
                    if(overview.mouse_lu || overview.mouse_ru)
                        sockets.packet.overview[i] = overview;
            }
        }
    return sockets;
}

static Sockets Clear(Sockets sockets)
{
    static Overview zero;
    for(int32_t i = 0; i < COLOR_COUNT; i++)
        sockets.packet.overview[i] = zero;
    return sockets;
}

Sockets Sockets_Relay(const Sockets sockets, const int32_t cycles, const int32_t interval)
{
    if((cycles % interval) == 0)
    {
        printf("%10d :: ", cycles);
        for(int32_t i = 0; i < COLOR_COUNT; i++)
            printf("%d ", sockets.sock[i] == NULL ? 0 : 1);
        putchar('\n');
        for(int32_t i = 0; i < COLOR_COUNT; i++)
        {
            TCPsocket sock = sockets.sock[i];
            if(sock)
                SDLNet_TCP_Send(sock, &sockets.packet, sizeof(sockets.packet));
        }
        return Clear(sockets);
    }
    return sockets;
}

Sockets Sockets_Accept(const Sockets sockets, const TCPsocket server)
{
    const TCPsocket client = SDLNet_TCP_Accept(server);
    return (client != NULL)
        ? Sockets_Add(sockets, client)
        : sockets;
}
