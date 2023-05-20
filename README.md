# LINFO2146

Project for the course LINFO2146 with my friends [@Félix](https://github.com/FelixGaudin) and [@Guillaume](https://github.com/gujadin)

## Where to put the code?

The code for the project is contained in the project directory. It should be placed under the contiki-ng/examples directory.

## Server/Border connection, and how to test the project

You should first open a port on the dockerized environment to establish a TCP connection with the border. So, you should add ```-p 60001:60001``` to the contiker alias command.

You can easily test the server/border connection by loading one simulation from the dedicated folder. Start the simulation, then run the server with the IP of the docker as well as the port 60001 as inputs (e.g.: `python3 server_test.py --ip 172.17.0.1 --port 60001`). You can check your IP for docker using `ip a` for example. You should then see received messages being printed.

After some time, the python program should start to display count values sent by the border at the end of each period. As the tree is being build, the first rounds will return zero values, but this will change after some time, as sensors and coordinators join the border node.