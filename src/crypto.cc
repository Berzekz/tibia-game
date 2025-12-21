#include "crypto.hh"

#include <openssl/err.h>
#include <openssl/pem.h>

#if OS_WINDOWS
// Required for OpenSSL to work with MSVC runtime FILE* operations
#include <openssl/applink.c>
#endif

static void DumpOpenSSLErrors(const char *Where, const char *What){
	error("OpenSSL error(s) while executing %s at %s:\n", What, Where);
	ERR_print_errors_cb(
		[](const char *str, usize len, void *u) -> int {
			// NOTE(fusion): These error strings already have trailing newlines,
			// for whatever reason.
			error("> %s", str);
			return 1;
		}, NULL);
}

// TRSAPrivateKey
// =============================================================================
TRSAPrivateKey::TRSAPrivateKey(void){
	m_RSA = NULL;
}

TRSAPrivateKey::~TRSAPrivateKey(void){
	if(m_RSA){
		RSA_free(m_RSA);
		m_RSA = NULL;
	}
}

bool TRSAPrivateKey::initFromFile(const char *FileName){
	if(m_RSA != NULL){
		error("TRSAPrivateKey::init: Key already initialized.\n");
		return false;
	}

	FILE *File = fopen(FileName, "rb");
	if(File == NULL){
		error("TRSAPrivateKey::initFromFile: Failed to open \"%s\".\n", FileName);
		return false;
	}

	m_RSA = PEM_read_RSAPrivateKey(File, NULL, NULL, NULL);
	fclose(File);

	if(m_RSA == NULL){
		error("TRSAPrivateKey::initFromFile: Failed to read key from \"%s\".\n", FileName);
		DumpOpenSSLErrors("TRSAPrivateKey::initFromFile", "PEM_read_RSAPrivateKey");
	}else if(RSA_size(m_RSA) != 128){
		error("TRSAPrivateKey::initFromFile: File \"%s\" doesn't contain a 1024-bit key", FileName);
		RSA_free(m_RSA);
		m_RSA = NULL;
	}

	return (m_RSA != NULL);
}

bool TRSAPrivateKey::decrypt(uint8 *Data){
	if(m_RSA == NULL){
		error("TRSAPrivateKey::decrypt: Key not initialized.\n");
		return false;
	}

	// TODO(fusion): Pass in the length of `Data` for checking.
	ASSERT(RSA_size(m_RSA) == 128);

	if(RSA_private_decrypt(128, Data, Data, m_RSA, RSA_NO_PADDING) == -1){
		DumpOpenSSLErrors("TRSAPrivateKey::decrypt", "RSA_private_decrypt");
		return false;
	}

	return true;
}

// TXTEASymmetricKey
// =============================================================================
void TXTEASymmetricKey::init(TReadBuffer *Buffer){
	m_SymmetricKey[0] = Buffer->readQuad();
	m_SymmetricKey[1] = Buffer->readQuad();
	m_SymmetricKey[2] = Buffer->readQuad();
	m_SymmetricKey[3] = Buffer->readQuad();
}

void TXTEASymmetricKey::encrypt(uint8 *Data){
	// TODO(fusion): This assumes both data endpoints have the same byte order.
	// It's unlikely that there is anything other than little-endian but we
	// should use a few helping functions to ensure compatibility.
	uint32 Sum = 0x00000000UL;
	uint32 Delta = 0x9E3779B9UL;
	uint32 V0 = *(uint32*)(&Data[0]);
	uint32 V1 = *(uint32*)(&Data[4]);
	for(int i = 0; i < 32; i += 1){
		V0 += (((V1 << 4) ^ (V1 >> 5)) + V1) ^ (Sum + m_SymmetricKey[Sum & 3]);
		Sum += Delta;
		V1 += (((V0 << 4) ^ (V0 >> 5)) + V0) ^ (Sum + m_SymmetricKey[(Sum >> 11) & 3]);
	}
	*(uint32*)(&Data[0]) = V0;
	*(uint32*)(&Data[4]) = V1;
}

void TXTEASymmetricKey::decrypt(uint8 *Data){
	// TODO(fusion): Same as above.
	uint32 Sum = 0xC6EF3720UL;
	uint32 Delta = 0x9E3779B9UL;
	uint32 V0 = *(uint32*)(&Data[0]);
	uint32 V1 = *(uint32*)(&Data[4]);
	for(int i = 0; i < 32; i += 1){
		V1 -= (((V0 << 4) ^ (V0 >> 5)) + V0) ^ (Sum + m_SymmetricKey[(Sum >> 11) & 3]);
		Sum -= Delta;
		V0 -= (((V1 << 4) ^ (V1 >> 5)) + V1) ^ (Sum + m_SymmetricKey[Sum & 3]);
	}
	*(uint32*)(&Data[0]) = V0;
	*(uint32*)(&Data[4]) = V1;
}
