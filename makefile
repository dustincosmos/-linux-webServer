server: main.cpp threadpool.h http_conn.cpp http_conn.h locker.h ./log/log.cpp ./log/log.h ./log/block_queue.h ./CGImysql/sql_connection_pool.cpp ./CGImysql/sql_connection_pool.h lst_timer.cpp lst_timer.h
	g++ -o server main.cpp threadpool.h http_conn.cpp http_conn.h locker.h ./log/log.cpp ./log/log.h ./log/block_queue.h ./CGImysql/sql_connection_pool.cpp ./CGImysql/sql_connection_pool.h lst_timer.cpp lst_timer.h -lpthread -lmysqlclient


clean:
	rm  -r server