# LINFO2146

Project for the course LINFO2146 with my friends [@Félix](https://github.com/FelixGaudin) and [@Guillaume](https://github.com/gujadin)

## TODO

- [x] The TODO list [@Félix](https://github.com/FelixGaudin)
- [ ] The **border** assigns a time slot to each **coordinator** node @Adrien
- [ ] The **coordinator** will inform the set of **sensor** nodes in its area
- [ ] The **sensor** sends there counter value to the **external server** via the **border** router [@Adrien](https://github.com/adribr53)
- [ ] The **sensor** checks if among the parents [@Félix](https://github.com/FelixGaudin)
  
  - [ ] If there is **a coordinator node**, if yes the coordinator node become the parent [@Félix](https://github.com/FelixGaudin)
  - [ ] If there is **multiple coordinator nodes**, the sensor node uses the signal strength to decide [@Félix](https://github.com/FelixGaudin)
  - [ ] If there are **only sensor nodes**, the node uses the signal strength to decide [@Félix](https://github.com/FelixGaudin)

- [ ] Connection **border** and **server** [@Adrien](https://github.com/adribr53)
- [ ] Routing protocol via Unicast/Multicast [@Félix](https://github.com/FelixGaudin)
- [ ] Produce fake data for **sensors**
- [ ] The **border** router has a fixed-size time window divided into time-slots (we can choose the duration of the time window and the duration of the slot) **Berkeley Time Synchronization Algorithm**

## Where to put the code?

The code for the project is contained in the project directory. It should be placed under the Contiki-ng directory.

## Server/Border connection

You should first open a port on the dockerized environment to establish a TCP connection with the border. So, you should add ```-p 60001:60001``` to the contiker alias command.

You can easily test the server/border connection by loading the simulation in *my_co_serv_to_border.csc*. Start the simulation, then run the server with the IP of the docker as well as the port 60001 as inputs (e.g.: `python3 server_test.py --ip 172.17.0.1 --port 60001`). You can check your IP for docker using `ip a` for example. You should then see received messages being printed.
