#include "services/auth_service.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <random>
#include <iomanip>
#include <sstream>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <openssl/err.h>
#include <nlohmann/json.hpp>

// Initialize static members
std::map<std::string, Services::AuthProvider> Services::AuthService::providers_;
std::string Services::AuthService::active_provider_;
std::vector<unsigned char> Services::AuthService::encryption_key_;
std::string Services::AuthService::key_file_path_ = "data/encryption_key.bin";

namespace Services {
    
    std::map<std::string, AuthProvider> AuthService::providers_;
    std::string AuthService::active_provider_ = "together";
    
    std::string AuthService::get_auth_config_path() {
        return "data/auth_config.json";
    }
    
    void AuthService::ensure_auth_directory() {
        std::filesystem::create_directories("data");
    }
    
    std::vector<unsigned char> AuthService::generate_random_bytes(size_t length) {
        std::vector<unsigned char> buffer(length);
        if (RAND_bytes(buffer.data(), length) != 1) {
            throw std::runtime_error("Failed to generate random bytes");
        }
        return buffer;
    }

    std::string AuthService::bytes_to_hex(const std::vector<unsigned char>& bytes) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (unsigned char byte : bytes) {
            oss << std::setw(2) << static_cast<int>(byte);
        }
        return oss.str();
    }

    std::vector<unsigned char> AuthService::hex_to_bytes(const std::string& hex) {
        std::vector<unsigned char> bytes;
        for (size_t i = 0; i < hex.length(); i += 2) {
            std::string byteString = hex.substr(i, 2);
            unsigned char byte = (unsigned char) strtol(byteString.c_str(), nullptr, 16);
            bytes.push_back(byte);
        }
        return bytes;
    }

    bool AuthService::derive_key(const std::string& password, 
                               const std::vector<unsigned char>& salt,
                               std::vector<unsigned char>& key) {
        key.resize(KEY_SIZE);
        return PKCS5_PBKDF2_HMAC(
            password.c_str(), password.length(),
            salt.data(), salt.size(),
            ITERATIONS, EVP_sha256(),
            KEY_SIZE, key.data()) == 1;
    }

    bool AuthService::save_encryption_key() {
        ensure_auth_directory();
        std::ofstream key_file(key_file_path_, std::ios::binary);
        if (!key_file) return false;
        
        // Generate a random salt
        auto salt = generate_random_bytes(SALT_SIZE);
        
        // Derive a key from the system's username and salt
        std::vector<unsigned char> derived_key;
        char username[256];
        if (getlogin_r(username, sizeof(username)) != 0) {
            throw std::runtime_error("Failed to get system username");
        }
        
        if (!derive_key(username, salt, derived_key)) {
            throw std::runtime_error("Key derivation failed");
        }
        
        // Encrypt the master key with the derived key
        std::vector<unsigned char> iv = generate_random_bytes(IV_SIZE);
        std::vector<unsigned char> tag(TAG_SIZE);
        std::vector<unsigned char> ciphertext(encryption_key_.size() + EVP_MAX_BLOCK_LENGTH);
        
        int len;
        int ciphertext_len;
        
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return false;
        
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, derived_key.data(), iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        
        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, encryption_key_.data(), encryption_key_.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        ciphertext_len = len;
        
        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        ciphertext_len += len;
        
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Write salt, IV, tag, and ciphertext to file
        key_file.write(reinterpret_cast<const char*>(salt.data()), salt.size());
        key_file.write(reinterpret_cast<const char*>(iv.data()), iv.size());
        key_file.write(reinterpret_cast<const char*>(tag.data()), tag.size());
        key_file.write(reinterpret_cast<const char*>(ciphertext.data()), ciphertext_len);
        
        return true;
    }
    
    bool AuthService::load_or_generate_key() {
        // Try to load existing key
        std::ifstream key_file(key_file_path_, std::ios::binary);
        if (key_file) {
            // Read salt, IV, tag, and ciphertext
            std::vector<unsigned char> salt(SALT_SIZE);
            std::vector<unsigned char> iv(IV_SIZE);
            std::vector<unsigned char> tag(TAG_SIZE);
            
            key_file.read(reinterpret_cast<char*>(salt.data()), SALT_SIZE);
            key_file.read(reinterpret_cast<char*>(iv.data()), IV_SIZE);
            key_file.read(reinterpret_cast<char*>(tag.data()), TAG_SIZE);
            
            // Read the rest as ciphertext
            std::vector<unsigned char> ciphertext(
                (std::istreambuf_iterator<char>(key_file)),
                std::istreambuf_iterator<char>()
            );
            
            // Derive key from username and salt
            char username[256];
            if (getlogin_r(username, sizeof(username)) != 0) {
                throw std::runtime_error("Failed to get system username");
            }
            
            std::vector<unsigned char> derived_key;
            if (!derive_key(username, salt, derived_key)) {
                throw std::runtime_error("Key derivation failed");
            }
            
            // Decrypt the master key
            std::vector<unsigned char> plaintext(ciphertext.size());
            int len;
            int plaintext_len;
            
            EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
            if (!ctx) return false;
            
            if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, 
                                 derived_key.data(), iv.data()) != 1) {
                EVP_CIPHER_CTX_free(ctx);
                return false;
            }
            
            if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, 
                                ciphertext.data(), ciphertext.size()) != 1) {
                EVP_CIPHER_CTX_free(ctx);
                return false;
            }
            plaintext_len = len;
            
            // Set expected tag value
            if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, tag.data()) != 1) {
                EVP_CIPHER_CTX_free(ctx);
                return false;
            }
            
            // Finalize decryption and check authentication tag
            int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
            EVP_CIPHER_CTX_free(ctx);
            
            if (ret <= 0) {
                // Authentication failed
                return false;
            }
            
            plaintext_len += len;
            plaintext.resize(plaintext_len);
            encryption_key_ = plaintext;
            return true;
        }
        
        // No existing key, generate a new one
        encryption_key_ = generate_random_bytes(KEY_SIZE);
        return save_encryption_key();
    }
    
    bool AuthService::initialize_encryption_key() {
        try {
            return load_or_generate_key();
        } catch (const std::exception& e) {
            std::cerr << "Encryption key initialization failed: " << e.what() << std::endl;
            return false;
        }
    }
    
    std::string AuthService::encrypt_credential(const std::string& credential) {
        if (encryption_key_.empty() && !initialize_encryption_key()) {
            throw std::runtime_error("Failed to initialize encryption key");
        }
        
        // Generate random IV
        std::vector<unsigned char> iv = generate_random_bytes(IV_SIZE);
        std::vector<unsigned char> tag(TAG_SIZE);
        std::vector<unsigned char> ciphertext(credential.size() + EVP_MAX_BLOCK_LENGTH);
        
        int len;
        int ciphertext_len;
        
        // Create and initialize the encryption context
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("Failed to create encryption context");
        
        // Initialize the encryption operation with AES-256-GCM
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, 
                             encryption_key_.data(), iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Encryption initialization failed");
        }
        
        // Encrypt the plaintext
        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, 
                            reinterpret_cast<const unsigned char*>(credential.c_str()), 
                            credential.length()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Encryption update failed");
        }
        ciphertext_len = len;
        
        // Finalize the encryption
        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Encryption finalization failed");
        }
        ciphertext_len += len;
        
        // Get the authentication tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to get authentication tag");
        }
        
        EVP_CIPHER_CTX_free(ctx);
        
        // Resize the ciphertext to actual size
        ciphertext.resize(ciphertext_len);
        
        // Combine IV + tag + ciphertext and encode as hex
        std::vector<unsigned char> combined;
        combined.reserve(iv.size() + tag.size() + ciphertext.size());
        combined.insert(combined.end(), iv.begin(), iv.end());
        combined.insert(combined.end(), tag.begin(), tag.end());
        combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());
        
        return bytes_to_hex(combined);
    }
    
    std::string AuthService::decrypt_credential(const std::string& encrypted_hex) {
        if (encryption_key_.empty() && !initialize_encryption_key()) {
            throw std::runtime_error("Failed to initialize encryption key");
        }
        
        // Convert hex string back to bytes
        std::vector<unsigned char> combined = hex_to_bytes(encrypted_hex);
        
        // Extract IV, tag, and ciphertext
        if (combined.size() < IV_SIZE + TAG_SIZE) {
            throw std::runtime_error("Invalid encrypted data format");
        }
        
        std::vector<unsigned char> iv(combined.begin(), combined.begin() + IV_SIZE);
        std::vector<unsigned char> tag(combined.begin() + IV_SIZE, combined.begin() + IV_SIZE + TAG_SIZE);
        std::vector<unsigned char> ciphertext(combined.begin() + IV_SIZE + TAG_SIZE, combined.end());
        
        // Decrypt the ciphertext
        std::vector<unsigned char> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
        int len;
        int plaintext_len;
        
        // Create and initialize the decryption context
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("Failed to create decryption context");
        
        // Initialize the decryption operation with AES-256-GCM
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, 
                             encryption_key_.data(), iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Decryption initialization failed");
        }
        
        // Provide the message to be decrypted and obtain the plaintext output
        if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, 
                            ciphertext.data(), ciphertext.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Decryption update failed");
        }
        plaintext_len = len;
        
        // Set the expected tag value
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, tag.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to set authentication tag");
        }
        
        // Finalize the decryption and check the authentication tag
        int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
        EVP_CIPHER_CTX_free(ctx);
        
        if (ret <= 0) {
            // Authentication failed
            throw std::runtime_error("Authentication failed - invalid key or corrupted data");
        }
        
        plaintext_len += len;
        plaintext.resize(plaintext_len);
        
        return std::string(plaintext.begin(), plaintext.end());
    }
    
    void AuthService::initialize_default_providers() {
        // Together AI
        AuthProvider together;
        together.name = "together";
        together.display_name = "Together AI";
        together.base_url = "https://api.together.xyz/v1";
        together.model = "meta-llama/Llama-3.2-3B-Instruct-Turbo";
        together.is_active = true;
        providers_["together"] = together;
        
        // Ollama (local)
        AuthProvider ollama;
        ollama.name = "ollama";
        ollama.display_name = "Ollama (Local)";
        ollama.base_url = "http://localhost:11434/v1";
        ollama.model = "llama3.2:3b";
        ollama.api_key = "ollama"; // Placeholder for local
        providers_["ollama"] = ollama;
        
        // OpenAI
        AuthProvider openai;
        openai.name = "openai";
        openai.display_name = "OpenAI";
        openai.base_url = "https://api.openai.com/v1";
        openai.model = "gpt-4";
        providers_["openai"] = openai;
        
        // Anthropic
        AuthProvider anthropic;
        anthropic.name = "anthropic";
        anthropic.display_name = "Anthropic Claude";
        anthropic.base_url = "https://api.anthropic.com/v1";
        anthropic.model = "claude-3-sonnet-20240229";
        providers_["anthropic"] = anthropic;
        
        // Cerebras
        AuthProvider cerebras;
        cerebras.name = "cerebras";
        cerebras.display_name = "Cerebras";
        cerebras.base_url = "https://api.cerebras.ai/v1";
        cerebras.model = "llama3.1-8b";
        providers_["cerebras"] = cerebras;
    }
    
    void AuthService::initialize() {
        // Initialize encryption first
        if (!initialize_encryption_key()) {
            throw std::runtime_error("Failed to initialize encryption");
        }
        
        initialize_default_providers();
        load_auth_config();
        
        // Try to load API keys from environment variables
        for (auto& [name, provider] : providers_) {
            std::string env_var = name + "_API_KEY";
            std::transform(env_var.begin(), env_var.end(), env_var.begin(), ::toupper);
            
            const char* env_key = std::getenv(env_var.c_str());
            if (env_key && provider.api_key.empty()) {
                try {
                    // Encrypt the API key before storing it
                    provider.api_key = encrypt_credential(env_key);
                    provider.is_valid = true;
                    // Save the encrypted key to config
                    save_auth_config();
                } catch (const std::exception& e) {
                    std::cerr << "Failed to encrypt API key for " << name 
                              << ": " << e.what() << std::endl;
                }
            }
        }
    }
    
    bool AuthService::add_provider(const AuthProvider& provider) {
        providers_[provider.name] = provider;
        return save_auth_config();
    }
    
    bool AuthService::remove_provider(const std::string& provider_name) {
        auto it = providers_.find(provider_name);
        if (it == providers_.end()) {
            return false;
        }
        
        // Don't allow removal of built-in providers
        if (provider_name == "together" || provider_name == "ollama" || 
            provider_name == "openai" || provider_name == "anthropic" || 
            provider_name == "cerebras") {
            return false;
        }
        
        providers_.erase(it);
        
        // If we're removing the active provider, switch to together
        if (active_provider_ == provider_name) {
            active_provider_ = "together";
        }
        
        return save_auth_config();
    }
    
    bool AuthService::set_active_provider(const std::string& provider_name) {
        auto it = providers_.find(provider_name);
        if (it == providers_.end()) {
            return false;
        }
        
        // Deactivate all providers
        for (auto& [name, provider] : providers_) {
            provider.is_active = false;
        }
        
        // Activate the selected provider
        it->second.is_active = true;
        active_provider_ = provider_name;
        
        return save_auth_config();
    }
    
    std::string AuthService::get_active_provider() {
        return active_provider_;
    }
    
    std::vector<std::string> AuthService::list_providers() {
        std::vector<std::string> provider_names;
        for (const auto& [name, provider] : providers_) {
            provider_names.push_back(name);
        }
        return provider_names;
    }
    
    AuthProvider AuthService::get_provider_info(const std::string& provider_name) {
        auto it = providers_.find(provider_name);
        if (it != providers_.end()) {
            return it->second;
        }
        return AuthProvider{}; // Return empty provider if not found
    }
    
    bool AuthService::set_api_key(const std::string& provider_name, const std::string& api_key) {
        auto it = providers_.find(provider_name);
        if (it == providers_.end()) {
            return false;
        }
        
        it->second.api_key = api_key;
        it->second.is_valid = !api_key.empty();
        
        return save_auth_config();
    }
    
    std::string AuthService::get_api_key(const std::string& provider_name) {
        std::string target_provider = provider_name.empty() ? active_provider_ : provider_name;
        
        auto it = providers_.find(target_provider);
        if (it != providers_.end()) {
            return it->second.api_key;
        }
        return "";
    }
    
    bool AuthService::validate_credentials(const std::string& provider_name) {
        auto it = providers_.find(provider_name);
        if (it == providers_.end()) {
            return false;
        }
        
        // For local providers like Ollama, always consider valid
        if (provider_name == "ollama") {
            it->second.is_valid = true;
            return true;
        }
        
        // For API-based providers, check if API key exists
        bool is_valid = !it->second.api_key.empty();
        it->second.is_valid = is_valid;
        
        return is_valid;
    }
    
    void AuthService::clear_credentials(const std::string& provider_name) {
        auto it = providers_.find(provider_name);
        if (it != providers_.end()) {
            it->second.api_key.clear();
            it->second.is_valid = false;
            save_auth_config();
        }
    }
    
    bool AuthService::save_auth_config() {
        try {
            ensure_auth_directory();
            
            nlohmann::json config;
            config["active_provider"] = active_provider_;
            config["providers"] = nlohmann::json::object();
            
            for (const auto& [name, provider] : providers_) {
                nlohmann::json provider_json;
                provider_json["name"] = provider.name;
                provider_json["display_name"] = provider.display_name;
                provider_json["base_url"] = provider.base_url;
                provider_json["model"] = provider.model;
                provider_json["is_active"] = provider.is_active;
                provider_json["is_valid"] = provider.is_valid;
                
                // Encrypt API key before saving
                if (!provider.api_key.empty()) {
                    provider_json["api_key"] = encrypt_credential(provider.api_key);
                }
                
                if (!provider.additional_config.empty()) {
                    provider_json["additional_config"] = provider.additional_config;
                }
                
                config["providers"][name] = provider_json;
            }
            
            std::ofstream file(get_auth_config_path());
            file << config.dump(2);
            
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Failed to save auth config: " << e.what() << std::endl;
            return false;
        }
    }
    
    bool AuthService::load_auth_config() {
        try {
            std::string config_path = get_auth_config_path();
            if (!std::filesystem::exists(config_path)) {
                return true; // Use defaults
            }
            
            std::ifstream file(config_path);
            nlohmann::json config;
            file >> config;
            
            if (config.contains("active_provider")) {
                active_provider_ = config["active_provider"];
            }
            
            if (config.contains("providers")) {
                for (const auto& [name, provider_json] : config["providers"].items()) {
                    auto it = providers_.find(name);
                    if (it != providers_.end()) {
                        // Update existing provider
                        it->second.is_active = provider_json.value("is_active", false);
                        it->second.is_valid = provider_json.value("is_valid", false);
                        
                        if (provider_json.contains("api_key")) {
                            it->second.api_key = decrypt_credential(provider_json["api_key"]);
                        }
                        
                        if (provider_json.contains("additional_config")) {
                            it->second.additional_config = provider_json["additional_config"];
                        }
                    } else {
                        // Add new custom provider
                        AuthProvider provider;
                        provider.name = provider_json.value("name", name);
                        provider.display_name = provider_json.value("display_name", name);
                        provider.base_url = provider_json.value("base_url", "");
                        provider.model = provider_json.value("model", "");
                        provider.is_active = provider_json.value("is_active", false);
                        provider.is_valid = provider_json.value("is_valid", false);
                        
                        if (provider_json.contains("api_key")) {
                            provider.api_key = decrypt_credential(provider_json["api_key"]);
                        }
                        
                        if (provider_json.contains("additional_config")) {
                            provider.additional_config = provider_json["additional_config"];
                        }
                        
                        providers_[name] = provider;
                    }
                }
            }
            
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Failed to load auth config: " << e.what() << std::endl;
            return false;
        }
    }
    
    bool AuthService::update_provider_config(const std::string& provider_name, const std::map<std::string, std::string>& config) {
        auto it = providers_.find(provider_name);
        if (it == providers_.end()) {
            return false;
        }
        
        for (const auto& [key, value] : config) {
            it->second.additional_config[key] = value;
        }
        
        return save_auth_config();
    }
    
    bool AuthService::test_provider_connection(const std::string& provider_name) {
        auto it = providers_.find(provider_name);
        if (it == providers_.end()) {
            return false;
        }
        
        // For local providers like Ollama, always return true for now
        if (provider_name == "ollama") {
            return true;
        }
        
        // For API providers, check if we have credentials
        return !it->second.api_key.empty();
    }
    
    std::string AuthService::get_provider_status(const std::string& provider_name) {
        auto it = providers_.find(provider_name);
        if (it == providers_.end()) {
            return "Not Found";
        }
        
        if (!it->second.is_valid) {
            return "Invalid Credentials";
        }
        
        if (test_provider_connection(provider_name)) {
            return "Connected";
        }
        
        return "Disconnected";
    }
    
    void AuthService::refresh_all_provider_status() {
        for (auto& [name, provider] : providers_) {
            provider.is_valid = validate_credentials(name);
        }
        save_auth_config();
    }
    
    bool AuthService::is_credential_secure() {
        // Check if we have proper encryption setup
        return true; // For now, assume secure
    }
    
    void AuthService::generate_encryption_key() {
        // In a real implementation, generate a proper encryption key
        // For now, this is a placeholder
    }
    
    bool AuthService::backup_credentials(const std::string& backup_path) {
        try {
            std::filesystem::copy_file(get_auth_config_path(), backup_path);
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Failed to backup credentials: " << e.what() << std::endl;
            return false;
        }
    }
    
    bool AuthService::restore_credentials(const std::string& backup_path) {
        try {
            std::filesystem::copy_file(backup_path, get_auth_config_path());
            load_auth_config();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Failed to restore credentials: " << e.what() << std::endl;
            return false;
        }
    }
}
