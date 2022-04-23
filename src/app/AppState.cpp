
#include <functional>

#include "AppState.hpp"
#include "crypto/Crypto.hpp"

AppState* AppState::singleton = NULL;

void KexMessage::GenerateDigest(Array32& hash) {
	digest::sha256()
		.absorb((uint8_t)error_code)
		.absorb(publicKey.data(), publicKey.size())
		.absorb(publicEcdheKey.data(), publicEcdheKey.size())
		.absorb(ipaddress.data(), ipaddress.size())
		.absorb(port)
		.finalize(hash.data());
}

bool KexMessage::Verify() {
	Array32 hash;
	GenerateDigest(hash);
	return ec::Verify(publicKey.data(), hash.data(), signature.data());
}

bool KexMessage::Sign(EcPrivateKey& privkey) {
	Array32 hash;
	GenerateDigest(hash);
	return ec::Sign(privkey.data(), hash.data(), signature.data());
}

AppState::AppState(std::string myIp, int32_t port) : rpcServer(port),
	ipAddress(myIp), port(port) {
		client = NULL;
		GenerateKey();
		BindAll();
		rpcServer.async_run(1);
		currentEncryptionMode = ENCRYPTION_MODE::CHACHA20_POLY1305;
		singleton = this;
}

AppState::~AppState() {
	if(client)
		delete client;
	if(singleton == this)
		singleton = NULL;
}

void AppState::BindAll() {
	rpcServer.bind("Message", [](Message msg)->uint32_t{
					return singleton->ReceiveMessage(msg);
				});
	rpcServer.bind("Kex", [](KexMessage msg)->KexMessage{
					return singleton->ReceiveKex(msg);
				});
}

void AppState::GenerateKey() {
	ec::GenKey(this->privateKey.data(), this->publicKey.data());
}

ERROR_CODE AppState::ConnectAndHandshake(std::string ip, int32_t port) {
	client = new rpc::client(ip, port);
	
	KexMessage kex;
	EcPrivateKey privateEcdheKey;
	if(ec::GenKey(privateEcdheKey.data(), kex.publicEcdheKey.data()) == false) {
		delete client;
		client = NULL;
		return FAILED;
	}
	kex.publicKey = publicKey;
	kex.error_code = SUCCESS;
	kex.ipaddress = ipAddress;
	kex.port = this->port;

	if(kex.Sign(this->privateKey) == false) {
		delete client;
		client = NULL;
		return FAILED;
	}
	
	KexMessage kexResponse = client->call("Kex", kex).as<KexMessage>();
	
	ERROR_CODE ret = kexResponse.error_code;
	
	if(kexResponse.error_code == SUCCESS) {
		if(kexResponse.Verify()) {
			if(ec::Ecdh(privateEcdheKey.data(),
						kexResponse.publicEcdheKey.data(), sharedKey.data())) {
				theirPublicKey = kexResponse.publicEcdheKey;
				return SUCCESS;
			} else {
				ret = FAILED;
			}
		} else {
			ret = FAILED_VALIDATION_KEX;
		}
	}
	if(ret != SUCCESS) {
		delete client;
		client = NULL;
	}
	return ret;
}

KexMessage AppState::ReceiveKex(KexMessage kexReceived) {
	if(kexReceived.Verify() == false) {
		return KexMessage{.error_code=FAILED_VALIDATION_KEX};
	}
	
	theirPublicKey = kexReceived.publicEcdheKey;
	
	KexMessage kex;
	if(ec::Ecdhe(kexReceived.publicEcdheKey.data(), kex.publicEcdheKey.data(),
				sharedKey.data()) == false) {
		return KexMessage{.error_code=FAILED};
	}
	kex.publicKey = publicKey;
	kex.error_code = SUCCESS;
	kex.ipaddress = ipAddress;
	kex.port = this->port;

	if(kex.Sign(this->privateKey) == false) {
		return KexMessage{.error_code=FAILED};
	}
	
	client = new rpc::client(kexReceived.ipaddress, kexReceived.port);
	
	return kex;
}



uint32_t AppState::SendMessage(std::string message) {
	if(client == NULL)
		return FAILED;
	Message msg;
	EncryptMessage(MSG, message.data(), message.size(), msg);
	return client->call("Message", msg).as<uint32_t>();
}

uint32_t AppState::ReceiveMessage(Message message) {
	std::vector<uint8_t> plaintext;
	bool res = DecryptMessage(message, plaintext);
	if(res) {
		if(message.msg_type == MSG) {
			std::string msg(plaintext.begin(), plaintext.end());
			PushMessage(msg);
			return SUCCESS;
		}
	}
	return INVALID_FUNCTION_PER_TYPE;
}

void AppState::PushMessage(const std::string& message) {
	std::lock_guard<std::mutex> l{mutex};
	receivedMessages.push(message);
}

bool AppState::PopMessage(std::string& message) {
	std::lock_guard<std::mutex> l{mutex};
	if(receivedMessages.empty())
		return false;
	message = receivedMessages.front();
	receivedMessages.pop();
	return true;
}



void AppState::EncryptMessage(MSG_TYPE type, const void* plaintext,
		size_t length, Message& message) {
	message.cipher_variant = currentEncryptionMode;
	message.msg_type = type;
	uint8_t ad[2] = {(uint8_t)message.msg_type,
		(uint8_t)message.cipher_variant};
	Encrypt(plaintext, length, message.nonce, message.encrypted_data, ad, 2,
			message.cipher_variant);
}

bool AppState::DecryptMessage(const Message& message,
		std::vector<uint8_t>& plaintext) {
	uint8_t ad[2] = {(uint8_t)message.msg_type,
		(uint8_t)message.cipher_variant};
	return Decrypt(message.encrypted_data.data(), message.encrypted_data.size(),
		   message.nonce, plaintext, ad, 2, message.cipher_variant);	
}



void AppState::Encrypt(const void* plaintext, size_t plaintextLength,
		ChachaNonce& nonce, std::vector<uint8_t>& ciphertext,
		const void* ad, size_t adLength, ENCRYPTION_MODE encryptionMode) {
	Random::Fill(nonce.data(), nonce.size());
	if(encryptionMode == CHACHA20) {
		ciphertext.resize(plaintextLength);
		chacha::crypt(sharedKey.data(), nonce.data(), plaintext,
				ciphertext.data(), plaintextLength, 0);
	} else if(encryptionMode == CHACHA20_POLY1305) {
		ciphertext.resize(plaintextLength + chacha::MAC_SIZE);
		chacha::encrypt(sharedKey.data(), nonce.data(), plaintext,
				ciphertext.data(), plaintextLength, ad, adLength);
	}
}

bool AppState::Decrypt(const void* ciphertext, size_t ciphertextLength,
		const ChachaNonce& nonce, std::vector<uint8_t>& plaintext,
		const void* ad, size_t adLength, ENCRYPTION_MODE encryptionMode) {
	if(encryptionMode == CHACHA20) {
		plaintext.resize(ciphertextLength);
		chacha::crypt(sharedKey.data(), nonce.data(), ciphertext,
				plaintext.data(), ciphertextLength, 0);
		return true;
	} else if(encryptionMode == CHACHA20_POLY1305) {
		plaintext.resize(ciphertextLength - chacha::MAC_SIZE);
		auto r = chacha::decrypt(sharedKey.data(), nonce.data(), ciphertext,
				plaintext.data(), ciphertextLength, ad, adLength);
		if(r > 0)
			return true;
	}
	return false;
}

