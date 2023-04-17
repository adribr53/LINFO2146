# LINFO2146
Project for the course LINFO2146 with my friends Félix and Guillaume

### Where to put the code ?
The code for the project is containted in the project directory. It should be placed under the contiki-ng directory. 

### Server/Border connection
You should first open a port on the dockerized environment to establish a tcp connection with the border. So, you should add ```-p 60001:60001``` to the contiker alias command.

You can easily test the server/border connection loading the simulation in *my_co_serv_to_border.csc*. Start the simulation, then run the server with the IP of the docker as well as the port 60001 as inputs (e.g. : ```python3 server_test.py --ip 172.17.0.1 --port 60001```). You can check your ip for docker using ```ip a``` for example. You should then see received messages being printed.