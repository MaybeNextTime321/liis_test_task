#include <string>
#include <sstream>
#include <json/value.h>
#include <json/reader.h>
#include <unordered_map>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Options.hpp> 
#include <set>
#include "mqtt/async_client.h"
#include <chrono>
#include <cstring>
#include <fstream>


class Сallback : public virtual mqtt::callback {
public:
	void connection_lost(const std::string& cause) override {
		std::cout << "\nConnection lost" << std::endl;
		if (!cause.empty())
			std::cout << "\tcause: " << cause << std::endl;
	}

	void delivery_complete(mqtt::delivery_token_ptr tok) override {
		std::cout << "\tDelivery complete for token: "
			<< (tok ? tok->get_message_id() : -1) << std::endl;
	}
};

int main() {

	std::unordered_map<std::string,double> result;
	const std::set<std::string> FIND_DEVICE_ID = {"S50", "S107","S60"};
	curlpp::Cleanup my_cleanup;
	std::string api_status = "";

	std::ostringstream os;
	os << curlpp::options::Url(std::string("https://api.data.gov.sg/v1/environment/air-temperature"));

	Json::Value root;
	Json::Reader reader;

	if( !reader.parse(os.str(),root) ){

		std::cerr << "Error with parsing JSON" << std::endl;
		return -1;
	}

	api_status = root["api_info"]["status"].asString();

	std::cout <<"Get api status: " <<  api_status << "\n";
	for(int i=0; i < root["items"][0]["readings"].size(); ++i){

		if(auto search_result = FIND_DEVICE_ID.find(root["items"][0]["readings"][i]["station_id"].asString());
			search_result != FIND_DEVICE_ID.end()){

			double value = root["items"][0]["readings"][i]["value"].asDouble();
			result.insert({*search_result,value});
		}
	}


	for(std::pair<std::string,double> single_item : result) {
		std::cout << "Get device data " << single_item.first << " " << single_item.second << std::endl;
	}



	const std::string SERVER_ADDRESS {"ssl://test.mosquitto.org:8885"};

	const std::string CLIENT_ID {"rw"};	

	const std::string PRIVATE_KEY {"client.key"};
	const std::string KEY_STORE {"client.crt"};
	const std::string TRUST_STORE {"mosquitto.org.crt"};

	const std::string TOPIC_STATUS {"api/status"};
	const std::string TOPIC_DEVICE_DATA {"api/temperature/"};

	const std::string MESSAGE = api_status;
	const std::string SSL_PASSWORD {"readwrite"};

	const int QOS = 1;
	const auto TIMEOUT = std::chrono::seconds(10);

	{
		std::ifstream tstore(TRUST_STORE);
		if(!tstore) {
			std::cerr << "The trust store file does not exist: " << TRUST_STORE << std::endl;
			return -1; 
		}

		std::ifstream kstore(KEY_STORE);
		if(!kstore) {
			std::cerr << "The key store file does not exist: " << KEY_STORE << std::endl;
			return -1;
		}

		std::ifstream private_key(PRIVATE_KEY);
		if(!private_key){
			std::cerr << "The private key file does not exist: " << KEY_STORE << std::endl;
			return -1;
		}
	}

	std::cout << "Initializing for server '" << SERVER_ADDRESS << "'..." << std::endl;
	mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);

	Сallback cb;
	client.set_callback(cb);

	auto sslops = mqtt::ssl_options_builder()
	    .trust_store(TRUST_STORE) 
	    .key_store(KEY_STORE)     
	    .private_key(PRIVATE_KEY) 
	    .finalize();

	auto willmsg = mqtt::message(TOPIC_STATUS, MESSAGE);

	auto connopts = mqtt::connect_options_builder()
	    .user_name(CLIENT_ID)
	    .password(SSL_PASSWORD)
	    .will(std::move(willmsg))
	    .ssl(std::move(sslops))
	    .finalize();


	std::cout << "  ...OK" << std::endl;

	try{

		std::cout << "\nConnecting..." << std::endl;


	    mqtt::token_ptr conntok = client.connect(connopts);
	    std::cout << "Connected successfully!" << std::endl;
	    conntok->wait();
	    std::cout << " ... Connection OK" << std::endl;

		std::cout << "\nSend data to toic: " << TOPIC_STATUS << " with value: " << MESSAGE << " " << std::endl;

		auto msg = mqtt::make_message(TOPIC_STATUS,MESSAGE,QOS,false);
		client.publish(msg)->wait_for(TIMEOUT);
		std::cout << "  ...OK" << std::endl;

		for(std::pair<std::string,double> single_item : result){

			std::string topic_address = TOPIC_DEVICE_DATA + single_item.first;
			std::string topic_data = std::to_string(single_item.second);
			std::cout << "Send data to toic: " << topic_address << " with value: " << topic_data << std::endl;
			auto data_msg = mqtt::make_message(topic_address,topic_data, QOS, false);
			client.publish(data_msg)->wait_for(TIMEOUT);
			std::cout << " ...OK" << std::endl;
		}

		std::cout << "\nDisconectiong..." << std::endl;
		client.disconnect()->wait();
		std::cout << "   ...OK" << std::endl;

	}
	catch (const mqtt::exception& exc) {

		std::cerr << exc.what() << std::endl;
	    auto reason_code = exc.get_reason_code();

	    if (reason_code) {

	        std::cerr << "MQTT Reason Code: " << static_cast<int>(reason_code) << std::endl;
	    }
	    
	    if (reason_code == 17) {
	        std::cerr << "MQTT Token Persistence Error: " << exc.get_message() << std::endl;
	    }

		return -1;
	}

	return 0;
}