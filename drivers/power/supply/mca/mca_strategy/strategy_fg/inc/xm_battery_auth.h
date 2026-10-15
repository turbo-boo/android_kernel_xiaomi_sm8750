
#ifndef __business_battery_AUTH_H
#define __business_battery_AUTH_H
#if 0
int StringToHex(char *str, unsigned char *out, unsigned int *outlen);
int fg_sha256_auth(struct bq_fg_chip *bq, u8 *rand_num, int length);
int set_verify_digest(u8 *rand_num);
int get_verify_digest(char *buf);
#endif

#endif /* __business_battery_AUTH_H */
