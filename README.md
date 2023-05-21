# LINFO2146

Project for the course LINFO2146 with my friends [@Félix](https://github.com/FelixGaudin) and [@Guillaume](https://github.com/gujadin)

## Where to put the code?

The code for the project is contained in the project directory. It should be placed under the contiki-ng/examples directory.

As for the simulations, they should be put in the root of the contiki project. The bash script ``` push_to_contiki.bash ``` should take care of that for you (do not mind the error messages for the rm commands, they typically occur when the code had not yet been moved to contiki). 3 cases are represented : one shows a sensor having children, one shows 4 coordinators with 4 sensors each, and the last one integrates a sensor having a child in the previous setting.

## Server/Border connection, and how to test the project

You should first open a port on the dockerized environment to establish a TCP connection with the border. So, you should add ```-p 60001:60001``` to the contiker alias command.

Note that, as documented in the report, one line of the file os/net/mac/csma/csma-output.c in contiki should be modified to enable a loop of more than two unicast messages.

You can easily test the server/border connection by loading one simulation from the dedicated folder. Start the simulation, then run the server with the IP of the docker as well as the port 60001 as inputs (e.g.: `python3 server_test.py --ip 172.17.0.1 --port 60001`). You can check your IP for docker using `ip a` for example. You should then see received messages being printed.

After some time, the python program should start to display count values sent by the border at the end of each period. As the tree is being build, the first rounds will return zero values, but this will change after some time, as sensors and coordinators join the border node.
