#include "rethink_db.hpp"

namespace com {
	namespace rethinkdb {

		//
		// definitions of exceptions
		//

		class connection_exception : public std::runtime_error{
		public:
			connection_exception() :runtime_error("RethinkDB connection exception occurred."){}
			connection_exception(const std::string& msg) :runtime_error(msg.c_str()){}
		};

		class runtime_error_exception : public std::runtime_error {
		public:
			runtime_error_exception() : runtime_error("RethinkDB runtime exception occurred."){}
			runtime_error_exception(const std::string& msg) : runtime_error(msg.c_str()){}
		};

		class compile_error_exception : public std::runtime_error {
		public:
			compile_error_exception() :runtime_error("RethinkDB compile error exception occurred."){}
			compile_error_exception(const std::string& msg) :runtime_error(msg.c_str()){}
		};

		class client_error_exception : public std::runtime_error {
		public:
			client_error_exception() :runtime_error("RethinkDB client error exception occurred."){}
			client_error_exception(const std::string& msg) :runtime_error(msg.c_str()){}
		};

		//
		// implementation of connection class
		//

		connection::connection(const std::string& host, const std::string& port, const std::string& database, const std::string& auth_key) : resolver_(io_service), socket_(io_service) {
			this->host = host;
			this->port = port;
			this->database = database;
			this->auth_key = auth_key;
			this->is_connected = false;

			this->connect();
		}

		bool connection::connect() {

			if (socket_.is_open() && this->is_connected) return true;

			try {
				// resolve the host
				boost::asio::ip::tcp::resolver::query query(this->host, this->port);
				boost::asio::ip::tcp::resolver::iterator iterator = this->resolver_.resolve(query);
				boost::asio::connect(this->socket_, iterator);

				// prepare stream for writing data
				std::ostream request_stream(&(this->request_));

				// write magic version number
				request_stream.write((char*)&(com::rethinkdb::VersionDummy::V0_2), sizeof (com::rethinkdb::VersionDummy::V0_2));

				// write auth_key length
				u_int auth_key_length = this->auth_key.length();
				request_stream.write((char*)&auth_key_length, sizeof (u_int));

				// write auth_key
				request_stream.write(this->auth_key.c_str(), auth_key.length());

				// send the request
				boost::asio::write(this->socket_, this->request_);

				// read response until a null character 
				boost::asio::read_until(this->socket_, this->response_, 0);

				// prepare to read a response
				std::istream response_stream(&(this->response_));
				std::string response;
				
				// read one line of response
				std::getline(response_stream, response);

				// if it starts with "SUCCESS"
				if (response.substr(0, 7) == "SUCCESS") {
					this->is_connected = true;
					return true;
				}
				else {
					this->is_connected = false;
					throw connection_exception("Error while connecting to RethinkDB server: "  +  response);
				}			
			} catch (std::exception& e)
			{
				// exception from boost has been caught
				throw connection_exception(std::string(e.what()));
			}			
		}

		void connection::create_db(std::string db_name) {
			com::rethinkdb::Query q = com::rethinkdb::Query();
			q.set_type(com::rethinkdb::Query::QueryType::Query_QueryType_START);

			// generate random token
			q.set_token(rand());

			com::rethinkdb::Term *t;
			t = q.mutable_query();
			t->set_type(com::rethinkdb::Term::TermType::Term_TermType_DB_CREATE);

			com::rethinkdb::Term *t_name;
			t_name = t->add_args();
			t_name->set_type(com::rethinkdb::Term::TermType::Term_TermType_DATUM);

			com::rethinkdb::Datum *datum;
			datum = t_name->mutable_datum();
			datum->set_type(com::rethinkdb::Datum::DatumType::Datum_DatumType_R_STR);
			datum->set_r_str(db_name);			

			try {
				// write query
				write_query(q);

				// read response
				std::shared_ptr<com::rethinkdb::Response> response(read_response());
				response->PrintDebugString();
			}
			catch (std::exception& e)
			{
				throw;
			}
		} 

		std::shared_ptr<com::rethinkdb::Response> connection::read_response() {
			u_int response_length;
			char* reply;
			size_t bytes_read;
			std::shared_ptr<com::rethinkdb::Response> response(new com::rethinkdb::Response());

			try {
				try {
					// read length of the response
					boost::asio::read(this->socket_, boost::asio::buffer(&response_length, sizeof(u_int)));

					// read content
					reply = new char[response_length];
					bytes_read = boost::asio::read(this->socket_, boost::asio::buffer(reply, response_length));
				} catch (std::exception& e) {
					throw connection_exception("Unable to read from the socket.");
				}			

				if (bytes_read != response_length) throw connection_exception(boost::str(boost::format("%1% bytes read, when %2% bytes promised.") % bytes_read % response_length));

				try {
					response->ParseFromArray(reply, response_length);
				} catch (std::exception& e) {
					throw connection_exception("Unable to parse the protobuf Response object.");
				}

				delete[] reply;
			}
			catch (std::exception& e) {
				throw connection_exception(boost::str(boost::format("Connection exception - read response: %1%") % e.what()));
			}

			return response;
		}

		void connection::write_query(const com::rethinkdb::Query& query) {
			// prepare output stream
			std::ostream request_stream(&request_);

			// serialize query
			std::string blob = query.SerializeAsString();

			// write blob's length in little endian 32 bit unsigned integer
			u_int blob_length = blob.length();
			request_stream.write((char*)&blob_length, sizeof (u_int));

			// write protobuf blob
			request_stream.write(blob.c_str(), blob.length());

			try {
				// send the content of the output stream over the wire
				boost::asio::write(this->socket_, this->request_);
			}
			catch (std::exception& e)
			{
				throw connection_exception(boost::str(boost::format("Connection exception - write query: exception: %1%") % e.what()));
			}
		}
	}
}