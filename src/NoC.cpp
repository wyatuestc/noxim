/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the Network-on-Chip
 */

#include "NoC.h"

using namespace std;

inline int toggleKthBit(int n, int k) 
{ 
    return (n ^ (1 << (k-1))); 
} 


void NoC::buildButterfly()
{

    buildCommon();
    
    //-----------------------------
    // --- 1- Switch bloc ---
    //-----------------------------

    int stg = log2(GlobalParams::butterfly_tiles);
    int sw = GlobalParams::butterfly_tiles/2; //sw: switch number in each stage
    
    int d = 1; //starting dir is changed at first iteration

    // Dimensions of the butterfly switch block network
    int dimX = stg;
    int dimY = sw;
    cout  << "tiles equal : " << GlobalParams::butterfly_tiles << endl;
    cout <<"dimX_stg= "<< dimX << "  " << "dimY_sw= " << dimY << endl ;
    req = new sc_signal_NSWEH<bool>*[dimX];
    ack = new sc_signal_NSWEH<bool>*[dimX];
    buffer_full_status = new sc_signal_NSWEH<TBufferFullStatus>*[dimX];
    flit = new sc_signal_NSWEH<Flit>*[dimX];

    // not used in butterfly
    free_slots = new sc_signal_NSWE<int>*[dimX];
    nop_data = new sc_signal_NSWE<NoP_data>*[dimX];

    // instantiation of the signal matrix
    // For each row (dimX) create a vector of DimY (columns)
    for (int i=0; i < dimX; i++) {
        req[i] = new sc_signal_NSWEH<bool>[dimY];
        ack[i] = new sc_signal_NSWEH<bool>[dimY];
	buffer_full_status[i] = new sc_signal_NSWEH<TBufferFullStatus>[dimY];
        flit[i] = new sc_signal_NSWEH<Flit>[dimY];

        free_slots[i] = new sc_signal_NSWE<int>[dimY];
        nop_data[i] = new sc_signal_NSWE<NoP_data>[dimY];
    }

    // instantiation of the Switches tiles matrix
    t = new Tile**[dimX];
    for (int i = 0; i < dimX; i++) {
    	t[i] = new Tile*[dimY];
    }


    // Create the mesh as a matrix of tiles
    for (int j = 0; j < dimY; j++) {
	for (int i = 0; i < dimX; i++) {
	    // Create the single Tile with a proper name
	
	// cout  << j <<  " " << i <<   endl;
	    char tile_name[30];
	    Coord tile_coord;
	    tile_coord.x = i;
	    tile_coord.y = j;
	    int tile_id = coord2Id(tile_coord); 
	    sprintf(tile_name, "Switch[%02d][%02d]_(#%d)", i, j, tile_id);
	    t[i][j] = new Tile(tile_name, tile_id);

	 //  cout << "switch  " << i <<  " " << j << "   has an Id = " << tile_id <<  endl;

	    // Tell to the router its coordinates
	    t[i][j]->r->configure(j * dimX + i,
				  GlobalParams::stats_warm_up_time,
				  GlobalParams::buffer_depth,
				  grtable);
	    t[i][j]->r->power.configureRouter(GlobalParams::flit_size,
		      			      GlobalParams::buffer_depth,
					      GlobalParams::flit_size,
					      string(GlobalParams::routing_algorithm),
					      "default");
					      


	    // Tell to the PE its coordinates
	    t[i][j]->pe->local_id = j * dimX + i;
	    t[i][j]->pe->traffic_table = &gttable;	// Needed to choose destination
	    t[i][j]->pe->never_transmit = (gttable.occurrencesAsSource(t[i][j]->pe->local_id) == 0);

	    // Map clock and reset
	    t[i][j]->clock(clock);
	    t[i][j]->reset(reset);
	   

	    // BFLY: hub connections work as usual
	    t[i][j]->hub_req_rx(req[i][j].from_hub);
	    t[i][j]->hub_flit_rx(flit[i][j].from_hub);
	    t[i][j]->hub_ack_rx(ack[i][j].to_hub);
	    t[i][j]->hub_buffer_full_status_rx(buffer_full_status[i][j].to_hub);

	    // signals/port when tile transmits(tx) to hub
	    t[i][j]->hub_req_tx(req[i][j].to_hub); // 7, sc_out
	    t[i][j]->hub_flit_tx(flit[i][j].to_hub);
	    t[i][j]->hub_ack_tx(ack[i][j].from_hub);
	    t[i][j]->hub_buffer_full_status_tx(buffer_full_status[i][j].from_hub);

        // TODO: Review port index. Connect each Hub to all its Channels 
        map<int, int>::iterator it = GlobalParams::hub_for_tile.find(tile_id);
        if (it != GlobalParams::hub_for_tile.end())
        {
            int hub_id = GlobalParams::hub_for_tile[tile_id];

            // The next time that the same HUB is considered, the next
            // port will be connected
            int port = hub_connected_ports[hub_id]++;

            hub[hub_id]->tile2port_mapping[t[i][j]->local_id] = port;

            hub[hub_id]->req_rx[port](req[i][j].to_hub);
            hub[hub_id]->flit_rx[port](flit[i][j].to_hub);
            hub[hub_id]->ack_rx[port](ack[i][j].from_hub);
            hub[hub_id]->buffer_full_status_rx[port](buffer_full_status[i][j].from_hub);

            hub[hub_id]->flit_tx[port](flit[i][j].from_hub);
            hub[hub_id]->req_tx[port](req[i][j].from_hub);
            hub[hub_id]->ack_tx[port](ack[i][j].to_hub);
            hub[hub_id]->buffer_full_status_tx[port](buffer_full_status[i][j].to_hub);

        }

	}
    } // original double for loop
 
    //---- Switching bloc connection ---- sw2sw mapping ---

    for (int i = 1; i < stg ; i++) 		//stg
    {
	for (int j = 0; j < sw ; j++) 		//sw 
	{   cout << "switch  " << i <<  " " << j << endl; 
	    int m = toggleKthBit(j, stg-i); cout << "m =  " << m << endl;    // m: var to flipping bit
	    int r = sw/(pow(2,i));  	       // change every r
	    int x = j%r; 
 //if ( i==1 & j==1) assert(false);
	    if (x==0) d = 1-d;
	    if (d==0)
		{
		t[i][j]->flit_rx[3](flit[i-1][j].north);
		t[i][j]->req_rx[3](req[i-1][j].north);
		t[i][j]->ack_rx[3](ack[i-1][j].west);
		t[i][j]->buffer_full_status_rx[3](buffer_full_status[i-1][j].west);
		//tx signals not required for butterfly
		t[i][j]->flit_tx[3](flit[i-1][j].north);
		t[i][j]->req_tx[3](req[i-1][j].north);
		t[i][j]->ack_tx[3](ack[i-1][j].west);
		t[i][j]->buffer_full_status_tx[3](buffer_full_status[i-1][j].west);


		t[i-1][j]->flit_tx[d](flit[i-1][j].north);
		t[i-1][j]->req_tx[d](req[i-1][j].north);
		t[i-1][j]->ack_tx[d](ack[i-1][j].west);
		t[i-1][j]->buffer_full_status_tx[d](buffer_full_status[i-1][j].west);
		//tx signals not required for butterfly
		t[i-1][j]->flit_rx[d](flit[i-1][j].north);
		t[i-1][j]->req_rx[d](req[i-1][j].north);
		t[i-1][j]->ack_rx[d](ack[i-1][j].west);
		t[i-1][j]->buffer_full_status_rx[d](buffer_full_status[i-1][j].west);


		t[i][j]->flit_rx[2](flit[i-1][m].north);
		t[i][j]->req_rx[2](req[i-1][m].north);
		t[i][j]->ack_rx[2](ack[i-1][m].south);
		t[i][j]->buffer_full_status_rx[2](buffer_full_status[i-1][m].south);
		//tx signals not required for butterfly
		t[i][j]->flit_tx[2](flit[i-1][m].north);
		t[i][j]->req_tx[2](req[i-1][m].north);
		t[i][j]->ack_tx[2](ack[i-1][m].south);
		t[i][j]->buffer_full_status_tx[2](buffer_full_status[i-1][m].south);


		t[i-1][m]->flit_tx[d](flit[i-1][m].north);
		t[i-1][m]->req_tx[d](req[i-1][m].north);
		t[i-1][m]->ack_tx[d](ack[i-1][m].south);
		t[i-1][m]->buffer_full_status_tx[d](buffer_full_status[i-1][m].south);
		//tx signals not required for butterfly
		t[i-1][m]->flit_rx[d](flit[i-1][m].north);
		t[i-1][m]->req_rx[d](req[i-1][m].north);
		t[i-1][m]->ack_rx[d](ack[i-1][m].south);
		t[i-1][m]->buffer_full_status_rx[d](buffer_full_status[i-1][m].south);
		} 
	    else 
		{
		t[i][j]->flit_rx[3](flit[i-1][j].east);
		t[i][j]->req_rx[3](req[i-1][j].east);
		t[i][j]->ack_rx[3](ack[i-1][j].south);
		t[i][j]->buffer_full_status_rx[3](buffer_full_status[i-1][j].south);
		//tx signals not required for butterfly
		t[i][j]->flit_tx[3](flit[i-1][j].east);
		t[i][j]->req_tx[3](req[i-1][j].east);
		t[i][j]->ack_tx[3](ack[i-1][j].south);
		t[i][j]->buffer_full_status_tx[3](buffer_full_status[i-1][j].south);

		t[i-1][j]->flit_tx[d](flit[i-1][j].east);
		t[i-1][j]->req_tx[d](req[i-1][j].east);
		t[i-1][j]->ack_tx[d](ack[i-1][j].south);
		t[i-1][j]->buffer_full_status_tx[d](buffer_full_status[i-1][j].south);
		//rx signals not required for butterfly
		t[i-1][j]->flit_rx[d](flit[i-1][j].east);
		t[i-1][j]->req_rx[d](req[i-1][j].east);
		t[i-1][j]->ack_rx[d](ack[i-1][j].south);
		t[i-1][j]->buffer_full_status_rx[d](buffer_full_status[i-1][j].south);

		t[i][j]->flit_rx[2](flit[i-1][m].east);
		t[i][j]->req_rx[2](req[i-1][m].east);
		t[i][j]->ack_rx[2](ack[i-1][m].west);
		t[i][j]->buffer_full_status_rx[2](buffer_full_status[i-1][m].west);
		//tx signals not required for butterfly
		t[i][j]->flit_tx[2](flit[i-1][m].east);
		t[i][j]->req_tx[2](req[i-1][m].east);
		t[i][j]->ack_tx[2](ack[i-1][m].west);
		t[i][j]->buffer_full_status_tx[2](buffer_full_status[i-1][m].west);

		t[i-1][m]->flit_tx[d](flit[i-1][m].east);
		t[i-1][m]->req_tx[d](req[i-1][m].east);
		t[i-1][m]->ack_tx[d](ack[i-1][m].west);
		t[i-1][m]->buffer_full_status_tx[d](buffer_full_status[i-1][m].west);
		//rx signals not required for butterfly
		t[i-1][m]->flit_rx[d](flit[i-1][m].east);
		t[i-1][m]->req_rx[d](req[i-1][m].east);
		t[i-1][m]->ack_rx[d](ack[i-1][m].west);
		t[i-1][m]->buffer_full_status_rx[d](buffer_full_status[i-1][m].west);
		}

	    // sw(1,0) connected to dir 0 of sw(0,0) -> dir 3
	    /*
	    // - the wire signal is the same
	    t[1][0]->flit_rx[3](flit[0][0].north);
	    t[0][0]->flit_tx[0](flit[0][0].north);

	    // sw(1,0) connected to dir 0 of sw(0,2) -> dir 2
	    t[1][0]->flit_rx[2](flit[0][2].north);
	    t[0][2]->flit_tx[0](flit[0][2].north);
	    */
	}
    }


    //-----------------------------
    // --- 2- Cores bloc ---
    //-----------------------------

    //---- Cores instantiation ----

    int n = GlobalParams::butterfly_tiles; //n: nombre of Cores = tiles with 2 directions(0 & 1)

    // Dimensions of the butterfly Cores : dimX=1 & dimY=n
        
    // instantiation of the Cores (we have only one row)
   
	core = new Tile*[n];

    // Create the Core bloc 
	
	for (int i = 0; i < n; i++) { 
	    // Create the single core with a proper name
	    char core_name[20];
	   // Coord core_coord;
	   //core_coord.x = i;
	   //core_coord.y = i;
	   //int core_id = coord2Id_bf(core_coord);
	    int core_id = (sw*stg)+i;
	    sprintf(core_name, "Core_(#%d)", core_id); cout<< "core_id = "<< core_id << endl;
	    core[i] = new Tile(core_name, core_id);

	    // Tell to the Core router its coordinates
	    core[i]->r->configure( i,
				  GlobalParams::stats_warm_up_time,
				  GlobalParams::buffer_depth,
				  grtable);
	    core[i]->r->power.configureRouter(GlobalParams::flit_size,
		      			      GlobalParams::buffer_depth,
					      GlobalParams::flit_size,
					      string(GlobalParams::routing_algorithm),
					      "default");
					      


	    // Tell to the PE its coordinates
	    core[i]->pe->local_id =  i;
	    core[i]->pe->traffic_table = &gttable;	// Needed to choose destination
	    core[i]->pe->never_transmit = (gttable.occurrencesAsSource(core[i]->pe->local_id) == 0);

	    // Map clock and reset
	    core[i]->clock(clock);
	    core[i]->reset(reset);
	} //-------------------------------------end core comment---------------------------------
	   
	// ---- Cores mapping---- 
	// Map RX and Tx (core2switch)

	   /* EXPLE:
	  // --First Stage--
	    // sw(0,0) connected to core(0) -> dir 3 (NB.the wire signal is the same)
	    t[0][0]->flit_rx[3](flit[0][3].west);
	    core[0]->flit_tx[0](flit[0][3].west);

	    // sw(0,0) connected to core(1) -> dir 2
	    t[0][0]->flit_rx[2](flit[0][2].south);
	    core[1]->flit_tx[0](flit[0][2].south);
	    */
	    /* 
          // --Last Stage--
	    // sw(2,0) connected to core(0) -> dir 0 (NB.the wire signal is the same)
	    t[2][0]->flit_tx[0](flit[2][0].north);
	    core[0]->flit_rx[1](flit[2][0].north);

	    // sw(2,0) connected to core(1) -> dir 1
	    t[2][0]->flit_tx[1](flit[2][0].east);
	    core[1]->flit_rx[1](flit[2][0].east);
	    */

	for (int i = 0; i < sw ; i++) 		
   	 {	
		t[0][i]->flit_rx[3](flit[0][3].west); // ack .west
		t[0][i]->req_rx[3](req[0][3].west);
		t[0][i]->ack_rx[3](ack[0][3].west);
		t[0][i]->buffer_full_status_rx[3](buffer_full_status[0][3].west);
		
		core[i*2]->flit_tx[0](flit[0][3].west);
		core[i*2]->req_tx[0](req[0][3].west);
		core[i*2]->ack_tx[0](ack[0][3].west);
		core[i*2]->buffer_full_status_tx[0](buffer_full_status[0][3].west);


		t[0][i]->flit_rx[2](flit[0][2].south); // ack .south
		t[0][i]->req_rx[2](req[0][2].south);
		t[0][i]->ack_rx[2](ack[0][2].south); 
		t[0][i]->buffer_full_status_rx[2](buffer_full_status[0][2].south);
				
		core[(i*2)+1]->flit_tx[0](flit[0][2].south);
		core[(i*2)+1]->req_tx[0](req[0][2].south);
		core[(i*2)+1]->ack_tx[0](ack[0][2].south);
		core[(i*2)+1]->buffer_full_status_tx[0](buffer_full_status[0][2].south);
	 }

	for (int i = 0; i < sw ; i++) 		
   	 {	
		t[stg-1][i]->flit_tx[0](flit[stg-1][i].north); // ack .east
		t[stg-1][i]->req_tx[0](req[stg-1][i].north);
		t[stg-1][i]->ack_tx[0](ack[stg-1][i].east);
		t[stg-1][i]->buffer_full_status_tx[0](buffer_full_status[stg-1][i].east);

		core[i*2]->flit_rx[1](flit[stg-1][i].north);
		core[i*2]->req_rx[1](req[stg-1][i].north);
		core[i*2]->ack_rx[1](ack[stg-1][i].east);
		core[i*2]->buffer_full_status_rx[1](buffer_full_status[stg-1][i].east);


		t[stg-1][i]->flit_tx[1](flit[stg-1][i].east);  // ack .east
		t[stg-1][i]->req_tx[1](req[stg-1][i].east);
		t[stg-1][i]->ack_tx[1](ack[stg-1][i].east);
		t[stg-1][i]->buffer_full_status_tx[1](buffer_full_status[stg-1][i].east);

		core[(i*2)+1]->flit_rx[1](flit[stg-1][i].east);
		core[(i*2)+1]->req_rx[1](req[stg-1][i].east);	
		core[(i*2)+1]->ack_rx[1](ack[stg-1][i].east); 
		core[(i*2)+1]->buffer_full_status_rx[1](buffer_full_status[stg-1][i].east);    
	 }// ---------------------------------end mapping code-----------------
   

//--- ---------------------------------- ---

    // dummy NoP_data structure
    NoP_data tmp_NoP;

    tmp_NoP.sender_id = NOT_VALID;

    for (int i = 0; i < DIRECTIONS; i++) {
	tmp_NoP.channel_status_neighbor[i].free_slots = NOT_VALID;
	tmp_NoP.channel_status_neighbor[i].available = false;
    }

    // Clear signals for borderline nodes

    // BFLY: change to adapt to the new butterfly topology
    // Border switches have valid connections
    // TODO: add not valid borders for Tiles that are not switches at the edge
    /*
    for (int i = 0; i <= GlobalParams::mesh_dim_x; i++) {
	req[i][0].south = 0;
	ack[i][0].north = 0;
	req[i][GlobalParams::mesh_dim_y].north = 0;
	ack[i][GlobalParams::mesh_dim_y].south = 0;

	free_slots[i][0].south.write(NOT_VALID);
	free_slots[i][GlobalParams::mesh_dim_y].north.write(NOT_VALID);

	nop_data[i][0].south.write(tmp_NoP);
	nop_data[i][GlobalParams::mesh_dim_y].north.write(tmp_NoP);

    }

    for (int j = 0; j <= GlobalParams::mesh_dim_y; j++) {
	req[0][j].east = 0;
	ack[0][j].west = 0;
	req[GlobalParams::mesh_dim_x][j].west = 0;
	ack[GlobalParams::mesh_dim_x][j].east = 0;

	free_slots[0][j].east.write(NOT_VALID);
	free_slots[GlobalParams::mesh_dim_x][j].west.write(NOT_VALID);

	nop_data[0][j].east.write(tmp_NoP);
	nop_data[GlobalParams::mesh_dim_x][j].west.write(tmp_NoP);

    }
    */
}

void NoC::buildCommon()
{
    token_ring = new TokenRing("tokenring");
    token_ring->clock(clock);
    token_ring->reset(reset);


    char channel_name[16];
    for (map<int, ChannelConfig>::iterator it = GlobalParams::channel_configuration.begin();
            it != GlobalParams::channel_configuration.end();
            ++it)
    {
        int channel_id = it->first;
        sprintf(channel_name, "Channel_%d", channel_id);
        channel[channel_id] = new Channel(channel_name, channel_id);
    }

    char hub_name[16];
    for (map<int, HubConfig>::iterator it = GlobalParams::hub_configuration.begin();
            it != GlobalParams::hub_configuration.end();
            ++it)
    {
        int hub_id = it->first;
	//LOG << " hub id " <<  hub_id;
        HubConfig hub_config = it->second;

        sprintf(hub_name, "Hub_%d", hub_id);
        hub[hub_id] = new Hub(hub_name, hub_id,token_ring);
        hub[hub_id]->clock(clock);
        hub[hub_id]->reset(reset);


        // Determine, from configuration file, which Hub is connected to which Tile
        for(vector<int>::iterator iit = hub_config.attachedNodes.begin(); 
                iit != hub_config.attachedNodes.end(); 
                ++iit) 
        {
            GlobalParams::hub_for_tile[*iit] = hub_id;
        }


        // Determine, from configuration file, which Hub is connected to which Channel
        for(vector<int>::iterator iit = hub_config.txChannels.begin(); 
                iit != hub_config.txChannels.end(); 
                ++iit) 
        {
            int channel_id = *iit;
            //LOG << "Binding " << hub[hub_id]->name() << " to txChannel " << channel_id << endl;
            hub[hub_id]->init[channel_id]->socket.bind(channel[channel_id]->targ_socket);
            //LOG << "Binding " << hub[hub_id]->name() << " to txChannel " << channel_id << endl;
            hub[hub_id]->setFlitTransmissionCycles(channel[channel_id]->getFlitTransmissionCycles(),channel_id);
        }

        for(vector<int>::iterator iit = hub_config.rxChannels.begin(); 
                iit != hub_config.rxChannels.end(); 
                ++iit) 
        {
            int channel_id = *iit;
            //LOG << "Binding " << hub[hub_id]->name() << " to rxChannel " << channel_id << endl;
            channel[channel_id]->init_socket.bind(hub[hub_id]->target[channel_id]->socket);
            channel[channel_id]->addHub(hub[hub_id]);
        }

	// TODO FIX
	// Hub Power model does not currently support different data rates for single hub
	// If multiple channels are connected to an Hub, the data rate
	// of the first channel will be used as default
	
	int no_channels = hub_config.txChannels.size();

	int data_rate_gbs;
	
	if (no_channels > 0) {
	    data_rate_gbs = GlobalParams::channel_configuration[hub_config.txChannels[0]].dataRate;
	}
	else
	    data_rate_gbs = NOT_VALID;

	// TODO: update power model (configureHub to support different tx/tx buffer depth in the power breakdown
	// Currently, an averaged value is used when accounting in Power class methods
	
	hub[hub_id]->power.configureHub(GlobalParams::flit_size,
		                        GlobalParams::hub_configuration[hub_id].toTileBufferSize,
		                        GlobalParams::hub_configuration[hub_id].fromTileBufferSize,
					GlobalParams::flit_size,
					GlobalParams::hub_configuration[hub_id].rxBufferSize,
					GlobalParams::hub_configuration[hub_id].txBufferSize,
					GlobalParams::flit_size,
					data_rate_gbs);
    }


    // Check for routing table availability
    if (GlobalParams::routing_algorithm == ROUTING_TABLE_BASED)
	assert(grtable.load(GlobalParams::routing_table_filename.c_str()));

    // Check for traffic table availability
    if (GlobalParams::traffic_distribution == TRAFFIC_TABLE_BASED)
	assert(gttable.load(GlobalParams::traffic_table_filename.c_str()));

    // Var to track Hub connected ports
    hub_connected_ports = (int *) calloc(GlobalParams::hub_configuration.size(), sizeof(int));
}
void NoC::buildMesh()
{


    buildCommon();

    // Initialize signals
    int dimX = GlobalParams::mesh_dim_x + 1;
    int dimY = GlobalParams::mesh_dim_y + 1;

    
    req = new sc_signal_NSWEH<bool>*[dimX];
    ack = new sc_signal_NSWEH<bool>*[dimX];
    buffer_full_status = new sc_signal_NSWEH<TBufferFullStatus>*[dimX];
    flit = new sc_signal_NSWEH<Flit>*[dimX];

    free_slots = new sc_signal_NSWE<int>*[dimX];
    nop_data = new sc_signal_NSWE<NoP_data>*[dimX];

    for (int i=0; i < dimX; i++) {
        req[i] = new sc_signal_NSWEH<bool>[dimY];
        ack[i] = new sc_signal_NSWEH<bool>[dimY];
	buffer_full_status[i] = new sc_signal_NSWEH<TBufferFullStatus>[dimY];
        flit[i] = new sc_signal_NSWEH<Flit>[dimY];

        free_slots[i] = new sc_signal_NSWE<int>[dimY];
        nop_data[i] = new sc_signal_NSWE<NoP_data>[dimY];
    }

    t = new Tile**[GlobalParams::mesh_dim_x];
    for (int i = 0; i < GlobalParams::mesh_dim_x; i++) {
    	t[i] = new Tile*[GlobalParams::mesh_dim_y];
    }


    // Create the mesh as a matrix of tiles
    for (int j = 0; j < GlobalParams::mesh_dim_y; j++) {
	for (int i = 0; i < GlobalParams::mesh_dim_x; i++) {
	    // Create the single Tile with a proper name
	    char tile_name[20];
	    Coord tile_coord;
	    tile_coord.x = i;
	    tile_coord.y = j;
	    int tile_id = coord2Id(tile_coord);
	    sprintf(tile_name, "Tile[%02d][%02d]_(#%d)", i, j, tile_id);
	    t[i][j] = new Tile(tile_name, tile_id);

	    // Tell to the router its coordinates
	    t[i][j]->r->configure(j * GlobalParams::mesh_dim_x + i,
				  GlobalParams::stats_warm_up_time,
				  GlobalParams::buffer_depth,
				  grtable);
	    t[i][j]->r->power.configureRouter(GlobalParams::flit_size,
		      			      GlobalParams::buffer_depth,
					      GlobalParams::flit_size,
					      string(GlobalParams::routing_algorithm),
					      "default");
					      


	    // Tell to the PE its coordinates
	    t[i][j]->pe->local_id = j * GlobalParams::mesh_dim_x + i;
	    t[i][j]->pe->traffic_table = &gttable;	// Needed to choose destination
	    t[i][j]->pe->never_transmit = (gttable.occurrencesAsSource(t[i][j]->pe->local_id) == 0);

	    // Map clock and reset
	    t[i][j]->clock(clock);
	    t[i][j]->reset(reset);

	    // Map Rx signals
	    t[i][j]->req_rx[DIRECTION_NORTH] (req[i][j].south);
	    t[i][j]->flit_rx[DIRECTION_NORTH] (flit[i][j].south);
	    t[i][j]->ack_rx[DIRECTION_NORTH] (ack[i][j].north);
	    t[i][j]->buffer_full_status_rx[DIRECTION_NORTH] (buffer_full_status[i][j].north);

	    t[i][j]->req_rx[DIRECTION_EAST] (req[i + 1][j].west);
	    t[i][j]->flit_rx[DIRECTION_EAST] (flit[i + 1][j].west);
	    t[i][j]->ack_rx[DIRECTION_EAST] (ack[i + 1][j].east);
	    t[i][j]->buffer_full_status_rx[DIRECTION_EAST] (buffer_full_status[i+1][j].east);

	    t[i][j]->req_rx[DIRECTION_SOUTH] (req[i][j + 1].north);
	    t[i][j]->flit_rx[DIRECTION_SOUTH] (flit[i][j + 1].north);
	    t[i][j]->ack_rx[DIRECTION_SOUTH] (ack[i][j + 1].south);
	    t[i][j]->buffer_full_status_rx[DIRECTION_SOUTH] (buffer_full_status[i][j+1].south);

	    t[i][j]->req_rx[DIRECTION_WEST] (req[i][j].east);
	    t[i][j]->flit_rx[DIRECTION_WEST] (flit[i][j].east);
	    t[i][j]->ack_rx[DIRECTION_WEST] (ack[i][j].west);
	    t[i][j]->buffer_full_status_rx[DIRECTION_WEST] (buffer_full_status[i][j].west);

	    // Map Tx signals
	    t[i][j]->req_tx[DIRECTION_NORTH] (req[i][j].north);
	    t[i][j]->flit_tx[DIRECTION_NORTH] (flit[i][j].north);
	    t[i][j]->ack_tx[DIRECTION_NORTH] (ack[i][j].south);
	    t[i][j]->buffer_full_status_tx[DIRECTION_NORTH] (buffer_full_status[i][j].south);

	    t[i][j]->req_tx[DIRECTION_EAST] (req[i + 1][j].east);
	    t[i][j]->flit_tx[DIRECTION_EAST] (flit[i + 1][j].east);
	    t[i][j]->ack_tx[DIRECTION_EAST] (ack[i + 1][j].west);
	    t[i][j]->buffer_full_status_tx[DIRECTION_EAST] (buffer_full_status[i + 1][j].west);

	    t[i][j]->req_tx[DIRECTION_SOUTH] (req[i][j + 1].south);
	    t[i][j]->flit_tx[DIRECTION_SOUTH] (flit[i][j + 1].south);
	    t[i][j]->ack_tx[DIRECTION_SOUTH] (ack[i][j + 1].north);
	    t[i][j]->buffer_full_status_tx[DIRECTION_SOUTH] (buffer_full_status[i][j + 1].north);

	    t[i][j]->req_tx[DIRECTION_WEST] (req[i][j].west);
	    t[i][j]->flit_tx[DIRECTION_WEST] (flit[i][j].west);
	    t[i][j]->ack_tx[DIRECTION_WEST] (ack[i][j].east);
	    t[i][j]->buffer_full_status_tx[DIRECTION_WEST] (buffer_full_status[i][j].east);

	    // TODO: check if hub signal is always required
	    // signals/port when tile receives(rx) from hub
	    t[i][j]->hub_req_rx(req[i][j].from_hub);
	    t[i][j]->hub_flit_rx(flit[i][j].from_hub);
	    t[i][j]->hub_ack_rx(ack[i][j].to_hub);
	    t[i][j]->hub_buffer_full_status_rx(buffer_full_status[i][j].to_hub);

	    // signals/port when tile transmits(tx) to hub
	    t[i][j]->hub_req_tx(req[i][j].to_hub); // 7, sc_out
	    t[i][j]->hub_flit_tx(flit[i][j].to_hub);
	    t[i][j]->hub_ack_tx(ack[i][j].from_hub);
	    t[i][j]->hub_buffer_full_status_tx(buffer_full_status[i][j].from_hub);

        // TODO: Review port index. Connect each Hub to all its Channels 
        map<int, int>::iterator it = GlobalParams::hub_for_tile.find(tile_id);
        if (it != GlobalParams::hub_for_tile.end())
        {
            int hub_id = GlobalParams::hub_for_tile[tile_id];

            // The next time that the same HUB is considered, the next
            // port will be connected
            int port = hub_connected_ports[hub_id]++;

            hub[hub_id]->tile2port_mapping[t[i][j]->local_id] = port;

            hub[hub_id]->req_rx[port](req[i][j].to_hub);
            hub[hub_id]->flit_rx[port](flit[i][j].to_hub);
            hub[hub_id]->ack_rx[port](ack[i][j].from_hub);
            hub[hub_id]->buffer_full_status_rx[port](buffer_full_status[i][j].from_hub);

            hub[hub_id]->flit_tx[port](flit[i][j].from_hub);
            hub[hub_id]->req_tx[port](req[i][j].from_hub);
            hub[hub_id]->ack_tx[port](ack[i][j].to_hub);
            hub[hub_id]->buffer_full_status_tx[port](buffer_full_status[i][j].to_hub);
        }

        // Map buffer level signals (analogy with req_tx/rx port mapping)
	    t[i][j]->free_slots[DIRECTION_NORTH] (free_slots[i][j].north);
	    t[i][j]->free_slots[DIRECTION_EAST] (free_slots[i + 1][j].east);
	    t[i][j]->free_slots[DIRECTION_SOUTH] (free_slots[i][j + 1].south);
	    t[i][j]->free_slots[DIRECTION_WEST] (free_slots[i][j].west);

	    t[i][j]->free_slots_neighbor[DIRECTION_NORTH] (free_slots[i][j].south);
	    t[i][j]->free_slots_neighbor[DIRECTION_EAST] (free_slots[i + 1][j].west);
	    t[i][j]->free_slots_neighbor[DIRECTION_SOUTH] (free_slots[i][j + 1].north);
	    t[i][j]->free_slots_neighbor[DIRECTION_WEST] (free_slots[i][j].east);

	    // NoP 
	    t[i][j]->NoP_data_out[DIRECTION_NORTH] (nop_data[i][j].north);
	    t[i][j]->NoP_data_out[DIRECTION_EAST] (nop_data[i + 1][j].east);
	    t[i][j]->NoP_data_out[DIRECTION_SOUTH] (nop_data[i][j + 1].south);
	    t[i][j]->NoP_data_out[DIRECTION_WEST] (nop_data[i][j].west);

	    t[i][j]->NoP_data_in[DIRECTION_NORTH] (nop_data[i][j].south);
	    t[i][j]->NoP_data_in[DIRECTION_EAST] (nop_data[i + 1][j].west);
	    t[i][j]->NoP_data_in[DIRECTION_SOUTH] (nop_data[i][j + 1].north);
	    t[i][j]->NoP_data_in[DIRECTION_WEST] (nop_data[i][j].east);

	}
    }

    // dummy NoP_data structure
    NoP_data tmp_NoP;

    tmp_NoP.sender_id = NOT_VALID;

    for (int i = 0; i < DIRECTIONS; i++) {
	tmp_NoP.channel_status_neighbor[i].free_slots = NOT_VALID;
	tmp_NoP.channel_status_neighbor[i].available = false;
    }


    // Clear signals for borderline nodes

    for (int i = 0; i <= GlobalParams::mesh_dim_x; i++) {
	req[i][0].south = 0;
	ack[i][0].north = 0;
	req[i][GlobalParams::mesh_dim_y].north = 0;
	ack[i][GlobalParams::mesh_dim_y].south = 0;

	free_slots[i][0].south.write(NOT_VALID);
	free_slots[i][GlobalParams::mesh_dim_y].north.write(NOT_VALID);

	nop_data[i][0].south.write(tmp_NoP);
	nop_data[i][GlobalParams::mesh_dim_y].north.write(tmp_NoP);

    }

    for (int j = 0; j <= GlobalParams::mesh_dim_y; j++) {
	req[0][j].east = 0;
	ack[0][j].west = 0;
	req[GlobalParams::mesh_dim_x][j].west = 0;
	ack[GlobalParams::mesh_dim_x][j].east = 0;

	free_slots[0][j].east.write(NOT_VALID);
	free_slots[GlobalParams::mesh_dim_x][j].west.write(NOT_VALID);

	nop_data[0][j].east.write(tmp_NoP);
	nop_data[GlobalParams::mesh_dim_x][j].west.write(tmp_NoP);

    }
}

Tile *NoC::searchNode(const int id) const
{
    for (int i = 0; i < GlobalParams::mesh_dim_x; i++)
	for (int j = 0; j < GlobalParams::mesh_dim_y; j++)
	    if (t[i][j]->r->local_id == id)
		return t[i][j];

    return NULL;
}

void NoC::asciiMonitor()
{ 
    //cout << sc_time_stamp().to_double()/GlobalParams::clock_period_ps << endl;
    system("clear");
    //
    // asciishow proof-of-concept #1 free slots
   
    for (int j = 0; j < GlobalParams::mesh_dim_y; j++)
    {
	for (int s = 0; s<3; s++)
	{
	    for (int i = 0; i < GlobalParams::mesh_dim_x; i++)
	    {
		if (s==0)
		    std::printf("|  %d  ",t[i][j]->r->buffer[s][0].getCurrentFreeSlots());
		else
		    if (s==1)
			std::printf("|%d   %d",t[i][j]->r->buffer[s][0].getCurrentFreeSlots(),t[i][j]->r->buffer[3][0].getCurrentFreeSlots());
		    else
			std::printf("|__%d__",t[i][j]->r->buffer[2][0].getCurrentFreeSlots());
	    }
	    cout << endl;
	}
    }
}

