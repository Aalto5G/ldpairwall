#include "reasshl.h"
#include "time64.h"

uint32_t reasshlhash_fn(struct hash_list_node *node, void *ud)
{
  return reasshlhash(CONTAINER_OF(node, struct reasshlentry, node));
}

void reasshlctx_expiry_fn(
  struct timer_link *timer, struct timer_linkheap *heap, void *ud, void *td)
{
  struct reasshlctx *hl = CONTAINER_OF(timer, struct reasshlctx, timer);
  struct allocif *loc = ud;
  struct linked_list_node *node, *tmp;
  uint32_t time64 = gettime64();
  LINKED_LIST_FOR_EACH_SAFE(node, tmp, &hl->list)
  {
    struct reasshlentry *e = CONTAINER_OF(node, struct reasshlentry, listnode);
    if (time64 > e->time64 + hl->timeout_secs*1000ULL*1000ULL)
    {
      rbcomboctx_free(loc, &e->combo);
      hash_table_delete(&hl->hash, &e->node, reasshlhash(e));
      linked_list_delete(&e->listnode);
      hl->mem_cur -= e->mem_cur;
      free(e);
      e = NULL;
    }
    else
    {
      // The entries are sorted, so if no match now, there won't be another
      break;
    }
  }
  hl->timer.time64 += hl->timer_secs*1000ULL*1000ULL;
  timer_linkheap_add(hl->heap, &hl->timer);
}

static void drop_oldest(struct reasshlctx *hl, struct allocif *loc)
{
  struct linked_list_node *node, *tmp;
  LINKED_LIST_FOR_EACH_SAFE(node, tmp, &hl->list)
  {
    struct reasshlentry *e = CONTAINER_OF(node, struct reasshlentry, listnode);
    if (hl->mem_cur <= hl->mem_limit)
    {
      break;
    }
    rbcomboctx_free(loc, &e->combo);
    hash_table_delete(&hl->hash, &e->node, reasshlhash(e));
    linked_list_delete(&e->listnode);
    hl->mem_cur -= e->mem_cur;
    free(e);
    e = NULL;
  }
}

struct packet *reasshlctx_add(struct reasshlctx *hl, struct allocif *loc,
                              void *pktdata, size_t pktsz, uint64_t time64)
{
  uint32_t src_ip, dst_ip;
  uint16_t id;
  uint8_t proto;
  void *ip;
  uint32_t hashval;
  struct hash_list_node *node;
  struct packet *pkt;
  struct reasshlentry *e;
  int ok;
  if (ether_type(pktdata) != ETHER_TYPE_IP)
  {
    abort();
  }
  ip = ether_payload(pktdata);
  proto = ip_proto(ip);
  id = ip_id(ip);
  src_ip = ip_src(ip);
  dst_ip = ip_dst(ip);
  pkt = allocif_alloc(loc, packet_size(pktsz));
  pkt->data = packet_calc_data(pkt);
  pkt->direction = 0;
  pkt->sz = pktsz;
  memcpy(pkt->data, pktdata, pktsz);
  hashval = reasshlhash_separate(src_ip, dst_ip, id, proto);
  HASH_TABLE_FOR_EACH_POSSIBLE(&hl->hash, node, hashval)
  {
    e = CONTAINER_OF(node, struct reasshlentry, node);
    ok = e->src_ip == src_ip &&
         e->dst_ip == dst_ip &&
         e->ip_id == id &&
         e->proto == proto;
    if (ok)
    {
      uint32_t old_mem = e->mem_cur;
      uint32_t new_mem;
      rbcomboctx_add(loc, &e->combo, pkt);
      pkt = NULL;
      if (e->combo.rfc_active)
      {
        new_mem = sizeof(struct rb815ctx) + sizeof(struct reasshlentry) + 32; 
      }
      else
      {
        new_mem = old_mem + packet_size(pktsz) + 16;
      }
      e->mem_cur = new_mem;
      hl->mem_cur += (new_mem - old_mem);
      drop_oldest(hl, loc);
      if (rbcomboctx_complete(&e->combo))
      {
        pkt = rbcomboctx_reassemble(loc, &e->combo);
        rbcomboctx_free(loc, &e->combo);
        hash_table_delete(&hl->hash, &e->node, hashval);
        linked_list_delete(&e->listnode);
        hl->mem_cur -= e->mem_cur;
        free(e);
        e = NULL;
      }
      return pkt;
    }
  }
  e = malloc(sizeof(*e));
  reasshlentry_init(e);
  e->src_ip = src_ip;
  e->dst_ip = dst_ip;
  e->ip_id = id;
  e->proto = proto;
  e->mem_cur = sizeof(struct reasshlentry) + packet_size(pktsz) + 32;
  e->time64 = time64;
  rbcomboctx_add(loc, &e->combo, pkt);
  linked_list_add_tail(&e->listnode, &hl->list);
  pkt = NULL;
  hl->mem_cur += e->mem_cur;
  hash_table_add_nogrow(&hl->hash, &e->node, hashval);
  drop_oldest(hl, loc);
  if (rbcomboctx_complete(&e->combo))
  {
    pkt = rbcomboctx_reassemble(loc, &e->combo);
    rbcomboctx_free(loc, &e->combo);
    hash_table_delete(&hl->hash, &e->node, hashval);
    linked_list_delete(&e->listnode);
    hl->mem_cur -= e->mem_cur;
    free(e);
    e = NULL;
  }
  return pkt;
}
