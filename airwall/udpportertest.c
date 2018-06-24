#include "udpporter.h"
#include <stdio.h>

struct udp_porter porter;

int main(int argc, char **argv)
{
  uint16_t port;
  size_t i;

  init_udp_porter(&porter);

  for (i = 0; i < 100; i++)
  {
    port = get_udp_port(&porter, (10<<24)|100, 1024);
    printf("%d\n", port);
    deallocate_udp_port(&porter, port);
  }
  return 0;
}
