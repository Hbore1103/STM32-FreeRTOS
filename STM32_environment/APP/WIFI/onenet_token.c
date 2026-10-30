#include "onenet_token.h"
#include "onenet.h"
#include "string.h"
#include "stdio.h"

/*
 * 自包含的 SHA1 / HMAC-SHA1 / Base64 / URL编码 实现
 * 仅为本工程OneNET鉴权服务，不依赖任何外部加密库
 */

/*=============================== SHA1 ===============================*/

typedef struct
{
	u32 state[5];
	u32 count[2];	//已处理比特数：count[0]低32位 count[1]高32位
	u8  buffer[64];
} SHA1_CTX;

#define SHA1_ROL(v,n)	(((v)<<(n))|((v)>>(32-(n))))

static void sha1_transform(u32 state[5],const u8 buffer[64])
{
	u32 a,b,c,d,e,t;
	u32 w[80];
	int i;

	for(i=0;i<16;i++)
	{
		w[i]=((u32)buffer[i*4]<<24)|((u32)buffer[i*4+1]<<16)
			|((u32)buffer[i*4+2]<<8)|((u32)buffer[i*4+3]);
	}
	for(i=16;i<80;i++)
	{
		w[i]=SHA1_ROL(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
	}

	a=state[0];b=state[1];c=state[2];d=state[3];e=state[4];

	for(i=0;i<80;i++)
	{
		u32 f,k;
		if(i<20)		{f=(b&c)|((~b)&d);		k=0x5A827999;}
		else if(i<40)	{f=b^c^d;				k=0x6ED9EBA1;}
		else if(i<60)	{f=(b&c)|(b&d)|(c&d);	k=0x8F1BBCDC;}
		else			{f=b^c^d;				k=0xCA62C1D6;}
		t=SHA1_ROL(a,5)+f+e+k+w[i];
		e=d;d=c;c=SHA1_ROL(b,30);b=a;a=t;
	}

	state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;
}

static void sha1_init(SHA1_CTX *ctx)
{
	ctx->state[0]=0x67452301;
	ctx->state[1]=0xEFCDAB89;
	ctx->state[2]=0x98BADCFE;
	ctx->state[3]=0x10325476;
	ctx->state[4]=0xC3D2E1F0;
	ctx->count[0]=0;
	ctx->count[1]=0;
}

static void sha1_update(SHA1_CTX *ctx,const u8 *data,u16 len)
{
	u16 i;
	u32 used;

	used=(ctx->count[0]>>3)&0x3F;	//当前缓冲已用字节
	ctx->count[0]+=len<<3;
	if(ctx->count[0]<(len<<3))
		ctx->count[1]++;
	ctx->count[1]+=len>>29;

	for(i=0;i<len;i++)
	{
		ctx->buffer[used++]=data[i];
		if(used==64)
		{
			sha1_transform(ctx->state,ctx->buffer);
			used=0;
		}
	}
}

static void sha1_final(SHA1_CTX *ctx,u8 digest[20])
{
	u8 bits[8];
	u32 used;
	int i,j;

	for(i=0;i<8;i++)
	{
		bits[i]=(u8)((ctx->count[(i>=4)?0:1]>>( (3-(i&3))*8 ))&0xFF);
	}

	used=(ctx->count[0]>>3)&0x3F;
	ctx->buffer[used++]=0x80;
	if(used>56)
	{
		while(used<64) ctx->buffer[used++]=0;
		sha1_transform(ctx->state,ctx->buffer);
		used=0;
	}
	while(used<56) ctx->buffer[used++]=0;
	for(i=0;i<8;i++) ctx->buffer[56+i]=bits[i];
	sha1_transform(ctx->state,ctx->buffer);

	for(j=0;j<5;j++)
	{
		digest[j*4]  =(u8)(ctx->state[j]>>24);
		digest[j*4+1]=(u8)(ctx->state[j]>>16);
		digest[j*4+2]=(u8)(ctx->state[j]>>8);
		digest[j*4+3]=(u8)ctx->state[j];
	}
}

static void sha1_one(const u8 *data,u16 len,u8 digest[20])
{
	SHA1_CTX ctx;
	sha1_init(&ctx);
	sha1_update(&ctx,data,len);
	sha1_final(&ctx,digest);
}

/*============================= HMAC-SHA1 =============================*/

static void hmac_sha1(const u8 *key,u16 key_len,const u8 *msg,u16 msg_len,u8 out[20])
{
	u8 k_ipad[64],k_opad[64],tk[20],inner[20];
	const u8 *kp;
	u16 kl;
	int i;

	if(key_len>64)
	{
		sha1_one(key,key_len,tk);
		kp=tk;
		kl=20;
	}
	else
	{
		kp=key;
		kl=key_len;
	}

	memset(k_ipad,0,64);
	memset(k_opad,0,64);
	memcpy(k_ipad,kp,kl);
	memcpy(k_opad,kp,kl);
	for(i=0;i<64;i++)
	{
		k_ipad[i]^=0x36;
		k_opad[i]^=0x5C;
	}

	{
		SHA1_CTX ctx;
		sha1_init(&ctx);
		sha1_update(&ctx,k_ipad,64);
		sha1_update(&ctx,msg,msg_len);
		sha1_final(&ctx,inner);

		sha1_init(&ctx);
		sha1_update(&ctx,k_opad,64);
		sha1_update(&ctx,inner,20);
		sha1_final(&ctx,out);
	}
}

/*============================== Base64 ==============================*/

static const char b64_tab[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

//Base64编码，输出带'='填充的C字符串
static void base64_encode(const u8 *in,u16 inlen,u8 *out)
{
	u16 i,j,k;

	for(i=0,j=0;i<inlen;)
	{
		u32 n=in[i++]<<16;
		k=1;							//本组已取字节数
		if(i<inlen){n|=in[i++]<<8;k++;}
		if(i<inlen){n|=in[i++];k++;}

		out[j++]=b64_tab[(n>>18)&0x3F];
		out[j++]=b64_tab[(n>>12)&0x3F];
		out[j++]=(k>=2)?b64_tab[(n>>6)&0x3F]:'=';	//剩2字节时此位仍有数据
		out[j++]=(k==3)?b64_tab[n&0x3F]:'=';
	}
	out[j]=0;
}

//取单个base64字符对应的6位值，非法字符返回-1
static int b64_value(u8 c)
{
	if(c>='A'&&c<='Z') return c-'A';
	if(c>='a'&&c<='z') return c-'a'+26;
	if(c>='0'&&c<='9') return c-'0'+52;
	if(c=='+') return 62;
	if(c=='/') return 63;
	return -1;
}

//Base64解码，返回解码后字节数
static u16 base64_decode(const u8 *in,u8 *out)
{
	u32 buf=0;
	int bits=0;
	u16 n=0;
	u8 c;
	int v;

	while((c=*in++)!=0)
	{
		if(c=='=')
			break;
		v=b64_value(c);
		if(v<0)
			continue;	//跳过换行等非base64字符
		buf=(buf<<6)|(u8)v;
		bits+=6;
		if(bits>=8)
		{
			bits-=8;
			out[n++]=(u8)((buf>>bits)&0xFF);
		}
	}
	return n;
}

/*============================= URL编码 ==============================*/

//除字母数字外全部按%XX编码（对应Python quote(safe='')），大写十六进制
static void url_encode(const u8 *in,u8 *out)
{
	static const char hex[]="0123456789ABCDEF";

	while(*in)
	{
		u8 c=*in++;
		if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9'))
		{
			*out++=c;
		}
		else
		{
			*out++='%';
			*out++=hex[c>>4];
			*out++=hex[c&0x0F];
		}
	}
	*out=0;
}

/*=========================== Token 组装入口 ===========================*/

#define TOKEN_VERSION		"2018-10-31"
#define TOKEN_METHOD		"sha1"
#define TOKEN_ET			"1893456000"	//2030-01-01 00:00:00 UTC，token到期时间

void OneNET_BuildToken(u8 *out,u16 out_len)
{
	u8 key_raw[64];			//设备密钥base64解码后的原始字节
	u8 key_len;
	u8 string_for_sign[128];//待签名串
	u8 digest[20];
	u8 sign_b64[40];
	u8 res_enc[96];
	u8 sign_enc[80];

	key_len=(u8)base64_decode((const u8*)ONENET_DEVICE_KEY,key_raw);

	//StringForSignature = et\nmethod\nres\nversion
	sprintf((char*)string_for_sign,"%s\n%s\nproducts/%s/devices/%s\n%s",
		TOKEN_ET,TOKEN_METHOD,ONENET_PRODUCT_ID,ONENET_DEVICE_NAME,TOKEN_VERSION);

	hmac_sha1(key_raw,key_len,string_for_sign,(u16)strlen((char*)string_for_sign),digest);
	base64_encode(digest,20,sign_b64);

	url_encode((const u8*)("products/" ONENET_PRODUCT_ID "/devices/" ONENET_DEVICE_NAME),res_enc);
	url_encode(sign_b64,sign_enc);

	sprintf((char*)out,"version=%s&res=%s&et=%s&method=%s&sign=%s",
		TOKEN_VERSION,res_enc,TOKEN_ET,TOKEN_METHOD,sign_enc);
	(void)out_len;
}
