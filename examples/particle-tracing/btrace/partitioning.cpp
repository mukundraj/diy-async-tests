#include "partitioning.h"
#include "../misc.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "message.h"
#include "advection.h"
#include <queue>


// enqueue remote data
// there is still a link, but you can send to any BlockID = (gid, proc)
void remote_enq(
        BBlock*,
        const diy::Master::ProxyWithLink&   cp,
        const diy::Assigner&                assigner, 
        datablock &db
        )
{
    // as a test, send my gid to block 2 gids away (in a ring), which is outside the link
    // (the link has only adjacent block gids)
    int my_gid              = cp.gid();
    int dest_gid            = db.to_proc;
    int dest_proc           = assigner.rank(dest_gid);
    diy::BlockID dest_block = {dest_gid, dest_proc};

    // db.gid = my_gid;
    cp.enqueue(dest_block, db);
}

static void remote_enq_blk_req(
    BBlock*,
    const diy::Master::ProxyWithLink&   cp,
    const diy::Assigner&                assigner, 
    data_req &msg
){
    int my_gid              = cp.gid();
    int dest_gid            = msg.to_proc;
    int dest_proc           = assigner.rank(dest_gid);
    diy::BlockID dest_block = {dest_gid, dest_proc};

    // db.gid = my_gid;
    cp.enqueue(dest_block, msg);
}

static void remote_enq_fulfill(
        BBlock*,
        const diy::Master::ProxyWithLink&   cp,
        const diy::Assigner&                assigner, 
        datablock &db

){
    int my_gid              = cp.gid();
    int dest_gid            = db.to_proc;
    int dest_proc           = assigner.rank(dest_gid);
    diy::BlockID dest_block = {dest_gid, dest_proc};

    // db.gid = my_gid;
    cp.enqueue(dest_block, db);

}

// fullfilling ghost cell request on receiver side
static void remote_deq_fulfill(
    BBlock* b, const diy::Master::ProxyWithLink& cp
){ 


   std::vector<int> incoming_gids;
    cp.incoming(incoming_gids);
    for (size_t i = 0; i < incoming_gids.size(); i++)
        if (cp.incoming(incoming_gids[i]).size())
        {   
            datablock recvd_data;
            cp.dequeue(incoming_gids[i], recvd_data);
            for(int i=0; i< recvd_data.cgid.size(); i++){
                int cid = recvd_data.cgid[i]; 
                b->data_ghost[cid] = recvd_data.data[i];
                b->bounds_ghost[cid] = recvd_data.bounds[i];
            }
        } 

}

// remote dequeuing block request
static void remote_deq_blk_req(BBlock* b, const diy::Master::ProxyWithLink& cp){
    
    b->data_reqsts.clear();

    std::vector<int> incoming_gids;
    cp.incoming(incoming_gids);
    for (size_t i = 0; i < incoming_gids.size(); i++)
        if (cp.incoming(incoming_gids[i]).size())
        {   
            // int recvd_data;
            data_req recvd_data;
            cp.dequeue(incoming_gids[i], recvd_data);
            // fmt::print(stderr, "Remote dequeued: gid {} received value {} from {}\n", cp.gid(), recvd_data.cids.size(), recvd_data.from_proc);
            b->data_reqsts.push_back(recvd_data);
            // for (size_t i=0; i<recvd_data.cgid.size(); i++){
            //     b->data[recvd_data.cgid[i]] = recvd_data.data[i];
            //     b->particles[recvd_data.cgid[i]] = std::move(recvd_data.particles[i]);
            //     b->bounds[recvd_data.cgid[i]] = std::move(recvd_data.bounds[i]);
            // }   
        }
}



/* Application defined query functions */

static int get_number_of_objects(void *data, int *ierr);
static void get_object_list(void *data, int sizeGID, int sizeLID,
            ZOLTAN_ID_PTR globalID, ZOLTAN_ID_PTR localID,
                  int wgt_dim, float *obj_wgts, int *ierr);
static int get_num_geometry(void *data, int *ierr);
static void get_geometry_list(void *data, int sizeGID, int sizeLID,
             int num_obj, ZOLTAN_ID_PTR globalID, ZOLTAN_ID_PTR localID,
             int num_dim, double *geom_vec, int *ierr);


static void init_zoltan_input(MESH_DATA &myMesh, std::vector<int> &partn, std::map<int, std::vector<float>> &data);

static void move_data(diy::mpi::communicator &world, int numImport, unsigned int*& importGlobalGids, int*& importProcs, int numExport, unsigned int*& exportGlobalGids, int*& exportProcs, std::map<int, std::vector<float>> & data);


void assign(diy::mpi::communicator &world, std::map<int, std::vector<float>> &data, std::map<int, std::vector<BEndPt>> particles, std::vector<int> &weights, std::vector<int> &partn, MESH_DATA &myMesh, BBlock* b, const diy::Master::ProxyWithLink &cp, const diy::RoundRobinAssigner &assigner, std::map<int,bbounds> &bounds){


    int argc; char **argv;
    float ver;
    int changes, numGidEntries, numLidEntries, numImport, numExport;
    ZOLTAN_ID_PTR importGlobalGids, importLocalGids, exportGlobalGids, exportLocalGids;
    int *importProcs, *importToPart, *exportProcs, *exportToPart;
    std::vector<int> parts;




	int rc = Zoltan_Initialize(argc, argv, &ver);



    
	
	if (rc != ZOLTAN_OK)
	{
		printf("sorry...\n");
		MPI_Finalize();
		exit(0);
	}
    struct Zoltan_Struct *zz;
    zz = Zoltan_Create(MPI_COMM_WORLD);

    /* General parameters */

    Zoltan_Set_Param(zz, "DEBUG_LEVEL", "0");
    Zoltan_Set_Param(zz, "LB_METHOD", "RCB");
    Zoltan_Set_Param(zz, "NUM_GID_ENTRIES", "1"); 
    Zoltan_Set_Param(zz, "NUM_LID_ENTRIES", "1");
    Zoltan_Set_Param(zz, "OBJ_WEIGHT_DIM", "1");
    Zoltan_Set_Param(zz, "RETURN_LISTS", "ALL");

    /* RCB parameters */

    Zoltan_Set_Param(zz, "RCB_OUTPUT_LEVEL", "0");
    // Zoltan_Set_Param(zz, "RCB_RECTILINEAR_BLOCKS", "1"); 
    Zoltan_Set_Param(zz, "RCB_RECTILINEAR_BLOCKS", "0"); 

    /* Query functions, to provide geometry to Zoltan */

    Zoltan_Set_Num_Obj_Fn(zz, get_number_of_objects, &myMesh);
    Zoltan_Set_Obj_List_Fn(zz, get_object_list, &myMesh);
    Zoltan_Set_Num_Geom_Fn(zz, get_num_geometry, &myMesh);
    Zoltan_Set_Geom_Multi_Fn(zz, get_geometry_list, &myMesh);

    /******************************************************************
  ** Zoltan can now partition the vertices in the simple mesh.
  ** In this simple example, we assume the number of partitions is
  ** equal to the number of processes.  Process rank 0 will own
  ** partition 0, process rank 1 will own partition 1, and so on.
  ******************************************************************/
  rc = Zoltan_LB_Partition(zz, /* input (all remaining fields are output) */
        &changes,        /* 1 if partitioning was changed, 0 otherwise */ 
        &numGidEntries,  /* Number of integers used for a global ID */
        &numLidEntries,  /* Number of integers used for a local ID */
        &numImport,      /* Number of vertices to be sent to me */
        &importGlobalGids,  /* Global IDs of vertices to be sent to me */
        &importLocalGids,   /* Local IDs of vertices to be sent to me */
        &importProcs,    /* Process rank for source of each incoming vertex */
        &importToPart,   /* New partition for each incoming vertex */
        &numExport,      /* Number of vertices I must send to other processes*/
        &exportGlobalGids,  /* Global IDs of the vertices I must send */
        &exportLocalGids,   /* Local IDs of the vertices I must send */
        &exportProcs,    /* Process to which I send each of the vertices */
        &exportToPart);  /* Partition to which each vertex will belong */

    // dprint("rank %d numExp %d, numImport %d", world.rank(), numExport, numImport);

//   if (world.rank() == 6)
//   {
//       for (int i = 0; i < numExport; i++)
//       {
//           dprint("exp torank %d gid %d", exportProcs[i], exportGlobalGids[i]);
//       }
//   }

//     if (world.rank() == 0)
//   {
//       for (int i = 0; i < numExport; i++)
//       {
//           dprint("exp fromrank %d gid %d", importProcs[i], importGlobalGids[i]);
//       }
//   }

    if (rc != ZOLTAN_OK)
	{
		printf("sorry...\n");
		MPI_Finalize();
		exit(0);
	}


  //move_data(world, numImport, importGlobalGids, importProcs, numExport, exportGlobalGids, exportProcs, data);

    

    // master.foreach([&](BBlock* b, const diy::Master::ProxyWithLink& cp)
    if (numExport>0){
        dprint("rank %d, numExport %d, cids [%d %d %d %d %d %d %d %d] toProcs [%d %d %d %d %d %d %d %d]", world.rank(), numExport, exportGlobalGids[0], exportGlobalGids[1], exportGlobalGids[2], exportGlobalGids[3], exportGlobalGids[4], exportGlobalGids[5], exportGlobalGids[6], exportGlobalGids[7], exportProcs[0], exportProcs[1],exportProcs[2],exportProcs[3],exportProcs[4],exportProcs[5],exportProcs[6],exportProcs[7]  );
    }
    std::map<int, datablock> stage;
    for (int i=0; i<numExport; i++){
       
        if (stage.find(exportProcs[i]) == stage.end()) { // if export procid has already added to stage
            datablock db;
            db.to_proc = exportProcs[i];
            db.from_proc = world.rank();
            db.data.push_back(std::move(data[exportGlobalGids[i]]));
            db.cgid.push_back(exportGlobalGids[i]);
            db.particles.push_back(std::move(particles[exportGlobalGids[i]]));
            db.bounds.push_back(std::move(bounds[exportGlobalGids[i]]));

            stage[exportProcs[i]] = db;
            data.erase(exportGlobalGids[i]);
            particles.erase(exportGlobalGids[i]);
            bounds.erase(exportGlobalGids[i]);
            // dprint("particles %ld", db.particles[i].size());
        }else{
             if (exportGlobalGids[i]==1){
                dprint("checkval %d bnd.min[0] %d", exportGlobalGids[i], bounds[exportGlobalGids[i]].min[0]);
            }
           stage[exportProcs[i]].data.push_back(std::move(data[exportGlobalGids[i]])); 
           stage[exportProcs[i]].cgid.push_back(exportGlobalGids[i]);
           stage[exportProcs[i]].particles.push_back(std::move(particles[exportGlobalGids[i]]));
           stage[exportProcs[i]].bounds.push_back(std::move(bounds[exportGlobalGids[i]]));

           data.erase(exportGlobalGids[i]);
            // it = data.find(exportGlobalGids[i]);
            data.erase(exportGlobalGids[i]);
            particles.erase(exportGlobalGids[i]);
            bounds.erase(exportGlobalGids[i]);

        }
        if (world.rank()==0)
            dprint("%d exporting %d to %d", world.rank(), exportGlobalGids[i], exportProcs[i]);
    }

    
    

    for ( auto &pair : stage ) {
        remote_enq(b, cp, assigner, pair.second); 
    }
    

  /******************************************************************
  ** Free the arrays allocated by Zoltan_LB_Partition, and free
  ** the storage allocated for the Zoltan structure.
  ******************************************************************/

  Zoltan_LB_Free_Part(&importGlobalGids, &importLocalGids, 
                      &importProcs, &importToPart);
  Zoltan_LB_Free_Part(&exportGlobalGids, &exportLocalGids, 
                      &exportProcs, &exportToPart);


    Zoltan_Destroy(&zz);

    /**********************
     ** all done ***********
    **********************/

    // if (myMesh.numMyPoints > 0){
    //     free(myMesh.myGlobalIDs);
    //     free(myMesh.x);
    //     free(myMesh.y);
    // }


}

static void move_data(diy::mpi::communicator &world, int numImport, unsigned int*& importGlobalGids, int*& importProcs, int numExport, unsigned int*& exportGlobalGids, int*& exportProcs, std::map<int, std::vector<float>> & data ){

    // create list of how many to send/recv to who (send count)
    std::vector<int> send_counts(world.size());
    std::vector<int> send_buf_gids(numExport);
    std::vector<float> send_buf_data;
    std::vector<int> sdispls(world.size());

    std::vector<int> recv_counts(world.size());
    std::vector<int> recv_buf_gids(numImport);
    std::vector<float> recv_buf_data(3*numImport);
    std::vector<int> rdispls(world.size());

    for (int i=0; i<send_buf_gids.size(); i++){
        send_buf_gids[i] = exportGlobalGids[i];
    }

    for (int i=0; i<numExport; i++){
        send_counts[exportProcs[i]] += 1;
    }

    for (int i=0; i<numImport; i++){
       recv_counts[importProcs[i]] += 1; 
    }

    for (size_t i=1; i<send_counts.size(); i++){
        sdispls[i] += sdispls[i-1];
    }

    for (size_t i=1; i<recv_counts.size(); i++){
        rdispls[i] += rdispls[i-1];
    }

   
        //populate the buffer to send data
       
        for (size_t i=0; i<send_buf_gids.size();i++){
            send_buf_data.resize((send_buf_data.size()+data[send_buf_gids[i]].size()));
             if (world.rank()==0)
                dprint("size %ld", data[send_buf_gids[i]].size());
             send_buf_data.insert(send_buf_data.end(),data[send_buf_gids[i]].begin(), data[send_buf_gids[i]].end() );
            
        }

        //  std::vector<long> send_bufferesizes; // size of data send to each proc
        // std::vector<long> buffersizes
        // // send the buffer sizes using all to all
        // int MPI_Alltoall(&send_buf_data, 1, MPI_LONG,
        //          void *recvbuf, 1, MPI_LONG, MPI_Comm comm)


       std::vector<long> recv_bufferesizes; // size of data received from each proc 
        // set the buffer sizes
        // for (size_t i=0; i<recv_buf_gids.size();i++){
        //     recv_buf_data.resize(recv_buf_data.size() + )
        // }

    // exchanging gid info
    MPI_Alltoallv(
        &send_buf_gids[0],
        &send_counts[0],
        &sdispls[0],
        MPI_INT,
        &recv_buf_gids[0],
        &recv_counts[0],
        &rdispls[0],
        MPI_INT,
        world
        );

    if (world.rank() == 0){
        dprint("recvcnts %ld", recv_counts.size());
        dprint("inc (%ld),  %d %d %d %d %d %d %d %d", recv_buf_gids.size(), recv_buf_gids[0], recv_buf_gids[1], recv_buf_gids[2], recv_buf_gids[3], recv_buf_gids[4], recv_buf_gids[5], recv_buf_gids[6], recv_buf_gids[7]);
        // dprint("inc (%ld)", recv_buf_data.size());
    }

    // exchanging data
    



    // share info with other procs


    // prepare data structures for all to all


    // do an all to all 

    // populate my data structures with received data


}


void partition(diy::mpi::communicator& world, bbounds &dom, int C, std::map<int, std::vector<float>> & data, int rank, int worldsize, std::vector<int> &cid_to_rank, int *cside, std::vector<int> &part, MESH_DATA &mesh_data, BBlock* b){
    
   
    // int S =  C * cellsperblockside // cells per side;
    // int side[3] = {(dom.max[0] - dom.min[0]+1)/C, (dom.max[0] - dom.min[0]+1)/C, (dom.max[0] - dom.min[0]+1)/C}; 
    // int num_per_side[3] =  {(dom.max[0] - dom.min[0]+1)/side[0], (dom.max[0] - dom.min[0]+1)/side[1], (dom.max[0] - dom.min[0]+1)/side[2]}; 
    // int corner_cell_id = rank / (C*C) + rank / C + rank % C;
    // int corner_cell_id =  rank / (num_per_side[0]) +  rank / num_per_side[1] +  rank % num_per_side[2];

    int rpd = worldsize / C; //{worldsize/C, worldsize/C, worldsize/C}; // ranks per dimension

    int c2 = rank / (rpd*rpd); // slowest varying
    int c1 = (rank % (rpd*rpd)) / rpd;
    int c0 = (rank % (rpd*rpd)) % rpd; // fastest varying
    
    
    int side[3] = {(dom.max[0] - dom.min[0]+1)/rpd, (dom.max[0] - dom.min[0]+1)/rpd, (dom.max[0] - dom.min[0]+1)/rpd }; // cells per side of a rank
    int coords[3] = {c2, c1, c0};

    int ccoords[3] =  {coords[0]*side[0], coords[1]*side[1], coords[2]*side[2]};// side of block
    dom.cside[0] = (dom.max[0] - dom.min[0]+1)/C; 
    dom.cside[1] = (dom.max[1] - dom.min[1]+1)/C; 
    dom.cside[2] = (dom.max[2] - dom.min[2]+1)/C; 

    // dprint("rank %d, cside %d %d %d", rank, dom.cside[0], dom.cside[1], dom.cside[2]);
    // dprint("rank %d, ccoords %d %d %d | rpd %d", rank, ccoords[0],  ccoords[1], ccoords[2], rpd);

    


    cside[0]= (dom.max[0] - dom.min[0]+1)/C;
    cside[1] =  (dom.max[0] - dom.min[0]+1)/C;
    cside[2] = (dom.max[0] - dom.min[0]+1)/C ; // cells per side of a block
    // dprint("bside %d %d %d", bside[0], bside[1], bside[2]);

    // populate the block ids into the map

    // int bpd = C; // cells per dimension
    // int T = C*C*C; // 

    // c2 = T / (bpd*bpd); // slowest varying
    // c1 = (T % (bpd*bpd)) / bpd;
    // c0 = (T % (bpd*bpd)) % bpd; // fastest varying

    int bpr = C/rpd;

    // dprint("corner block ids rank %d | %d %d %d", rank, coords[0]*bpr, coords[1]*bpr, coords[2]*bpr);
    // dprint("rank block corner cells %d | bid %d %d %d", rank, coords[0]*bpr*side[0], coords[1]*bpr*side[0], coords[2]*bpr*side[0]);

    // if (rank==0)
    //     dprint("sides %d %d %d | bpr %d", side[0], side[1], side[2], bpr);



    // add the cell ids to the map and set current rank in cid_to_rank
    
    for (int i=coords[0]*bpr; i<coords[0]*bpr+bpr; i++){
        for (int j=coords[1]*bpr; j< coords[1]*bpr+bpr; j++){
            for (int k=coords[2]*bpr; k < coords[2]*bpr+bpr; k++){
                int bid = i*C*C + j*C + k;
                // dprint ("rank %d, bid %d", rank, bid);

                std::vector<float> temp(3*cside[0]*cside[1]*cside[2]);
                std::pair<int,std::vector<float>> tpair (bid,temp);
                data.insert(tpair);
               
                cid_to_rank[bid] = rank;

                bbounds bnd;
                // bnd.cside[0] = cside[0];
                // bnd.cside[1] = cside[1];
                // bnd.cside[2] = cside[2];

                gid2bounds(bid, &cside[0], C, dom, bnd);
                // dprint("cid %d, rank %d, bnd %d %d %d |cside %d %d %d |%d", bid, world.rank(), bnd.max[0], bnd.max[1], bnd.max[2], bnd.cside[0], bnd.cside[1], bnd.cside[2], dom.max[2]);
                std::pair<int,bbounds> tpair2(bid,bnd);
                b->bounds.insert(tpair2);

            }
        }
    }
    part.resize(cid_to_rank.size());
    MPI_Allreduce(&cid_to_rank[0], &part[0], cid_to_rank.size(), MPI_INT, MPI_SUM, world);


    dprint("data size %d, %ld", data.size(), cid_to_rank.size());


    // init the Zoltan mesh
    init_zoltan_input(mesh_data, part, data);
    



}
    // dprint("map size %ld", data.size());
// https://stackoverflow.com/questions/20834838/using-tuple-in-unordered-map


/* Application defined query functions */

static int get_number_of_objects(void *data, int *ierr)
{
  MESH_DATA *mesh= (MESH_DATA *)data;
  *ierr = ZOLTAN_OK;
  return mesh->numMyPoints;
}

static void get_object_list(void *data, int sizeGID, int sizeLID,
            ZOLTAN_ID_PTR globalID, ZOLTAN_ID_PTR localID,
                  int wgt_dim, float *obj_wgts, int *ierr)
{
int i;
  MESH_DATA *mesh= (MESH_DATA *)data;
  *ierr = ZOLTAN_OK;

  /* In this example, return the IDs of our objects, but no weights.
   * Zoltan will assume equally weighted objects.
   */

  for (i=0; i<mesh->numMyPoints; i++){
    globalID[i] = mesh->myGlobalIDs[i];
    localID[i] = i;
    obj_wgts[i] = 1;
  }
}

static int get_num_geometry(void *data, int *ierr)
{
  *ierr = ZOLTAN_OK;
  return 3;
}

static void get_geometry_list(void *data, int sizeGID, int sizeLID,
                      int num_obj,
             ZOLTAN_ID_PTR globalID, ZOLTAN_ID_PTR localID,
             int num_dim, double *geom_vec, int *ierr)
{
int i;

  MESH_DATA *mesh= (MESH_DATA *)data;

  if ( (sizeGID != 1) || (sizeLID != 1) || (num_dim != 3)){
    *ierr = ZOLTAN_FATAL;
    return;
  }

  *ierr = ZOLTAN_OK;

  for (i=0;  i < num_obj ; i++){
    geom_vec[3*i] = (double)mesh->x[i];
    geom_vec[3*i + 1] = (double)mesh->y[i];
    geom_vec[3*i + 2] = (double)mesh->z[i];
  }

  return;
}


void init_zoltan_input(MESH_DATA &myMesh, std::vector<int> &partn, std::map<int, std::vector<float>> & data)
{

    // count and id my points
    int nobj = (int) data.size();
    myMesh.numMyPoints = nobj;
    
    std::map<int, std::vector<float>>::iterator it = data.begin();

    myMesh.myGlobalIDs = (ZOLTAN_ID_TYPE *)malloc(sizeof(ZOLTAN_ID_TYPE) * nobj);
    myMesh.x = (float *)malloc(sizeof(float) * nobj);
    myMesh.y = (float *)malloc(sizeof(float) * nobj);
    myMesh.z = (float *)malloc(sizeof(float) * nobj);
    
    int i=0;
    while (it != data.end()){

        myMesh.myGlobalIDs[i] = it->first;
        myMesh.x[i] = it->second[i];
        myMesh.y[i] = it->second[nobj+i];
        myMesh.z[i] = it->second[2*nobj+i];

        i++;
        it++;
    }



    // copy 




}


void update_cid_to_rank_vector(diy::mpi::communicator &world, std::map<int, std::vector<float>> &data, std::vector<int> &cid_to_rank){

    // reset cid_to_rank vector

    std::fill(cid_to_rank.begin(), cid_to_rank.end(), 0);

    std::vector<int> tmp(cid_to_rank.size());

    // populate current cell positions in the cid_to_rank vector
    std::map<int, std::vector<float>>::iterator it = data.begin();
    while (it != data.end()){
        tmp[it->first] = world.rank();
        it++;
    }

   // all reduce the cid to rank vector
   MPI_Allreduce(&tmp[0], &cid_to_rank[0], cid_to_rank.size(), MPI_INT, MPI_SUM, world);

}

void get_nbrs_of_cell(int cid, std::vector<int>& nbrs, bbounds &bnd, bbounds &dom, int C, int rank){

    // to do 
    // pass in global bounds, use it's cside and also check if it is inside
    
    int i,j,k;

    int ctr = 0;
    int x,y,z;
    // if (cid==1){
    for (int i=-1; i<2; i++){
        for (int j=-1; j<2; j++){
            for (int k=-1; k<2; k++){
                

                if (ctr!=13){
                    x = bnd.min[0] + i*dom.cside[0];
                    y = bnd.min[1] + j*dom.cside[1];
                    z = bnd.min[2] + k*dom.cside[2];
                    
                    if (x >= dom.min[0] && x < dom.max[0] & 
                        y >= dom.min[1] && y < dom.max[1] &
                        z >= dom.min[2] && z < dom.max[2] ){
                            int ccid = pos2cgid((float)x, (float)y, (float)z, dom, C);
                            // dprint("ctr %d (%d %d %d), ccid %d , (%d %d %d), %d %d, rank %d", ctr, i, j, k, ccid, x,y,z, bnd.min[2], dom.cside[2], rank);
                            nbrs.push_back(ccid);
                        }
                }
                ctr++;

            }
        }
    }



        
    //  dprint("getting nbr of %d, size %ld", cid, nbrs.size());
    // }

   
    
}


void id_ghost_cells(std::map<int, std::vector<float>> &data, std::map<int, std::vector<float>> &data_ghost, std::map<int, bbounds> &bounds, bbounds &dom, int C, int rank){

    std::queue<int> queue;

    // push cells in block into queue 
    std::map<int, std::vector<float>>::iterator it = data.begin();
    while (it != data.end()){
        
        queue.push(it->first);
        it++;
    }

    // if (rank==6)
    //     dprint("queue %d size %ld", rank, queue.size());
    std::vector<int> ghost_cids;

    

    // while queue not empty
    while(queue.size()>0){

        int ccid = queue.front();
        queue.pop();

        // get nbrs of cells
        std::vector<int> nbrs;
        
        // if (ccid==1){
        //     dprint("ccid %d rank %d", ccid, rank);
        //     dprint("ccid %d rank %d, datasize %ld %d", ccid, rank, data[ccid].size(), bounds[ccid].min[0]);
        //     print_cellids_in_block(data);
        // }
        get_nbrs_of_cell(ccid, nbrs, bounds[ccid], dom, C, rank);

        if (rank==6){
            dprint("**** got nbrs %d, %ld, ccid %d, bounds(%d %d, %d %d, %d %d), bndsize %ld", rank, nbrs.size(), ccid, bounds[ccid].min[0], bounds[ccid].max[0], bounds[ccid].min[1], bounds[ccid].max[1], bounds[ccid].min[2], bounds[ccid].max[2], bounds.size());
            // pvi(nbrs);
        }
        // push nbrs in queue and ghost_cids if not already in block
        for (auto &nbr_cid: nbrs){
            if (data.find(nbr_cid)==data.end()){
                ghost_cids.push_back(nbr_cid);
            }
        }
        

    }
    // push the ghost ids to b->data_ghost
    for (auto &cid: ghost_cids){
        std::vector<float> tmp;
        data_ghost.insert(std::make_pair(cid, tmp));
        
    }
     dprint("ghost cids %ld, rank %d", ghost_cids.size(), rank);
}


void get_ghost_cells(diy::Master &master, const diy::RoundRobinAssigner &assigner, int N, bbounds &dom, int C, int rank){


    // id ghost cells
    master.foreach ([&](BBlock *b, const diy::Master::ProxyWithLink &cp) {
        id_ghost_cells(b->data, b->data_ghost, b->bounds, dom, C, rank);
        // dprint("data ghost %d, rank %d", b->data_ghost.size(), rank);
         // enqueue request for ghost cells using b->cid_to_rank using b->gid_to_rank

        std::map<int, data_req> stage;

        std::map<int, std::vector<float>>::iterator it = b->data_ghost.begin();
        
        while (it != b->data_ghost.end()){
             int dest = b->cid_to_rank[it->first];
             int ccid = it->first;

             if (stage.find(dest) == stage.end())
             { // if export procid has already added to stage
                 data_req db;

                 db.to_proc = dest;
                 db.from_proc = rank;
                 db.cids.push_back(ccid);
                 stage[dest] = db;
             }
            else
             {
                 stage[dest].cids.push_back(ccid);
             }
            it++;
        }

        for ( auto &pair : stage ) {
            remote_enq_blk_req(b, cp, assigner, pair.second); 
        }
    
    });


    // do rexchange
    bool remote = true;
    master.exchange(remote);
    master.foreach(&remote_deq_blk_req);

    
    // enqueue back ghost cells data to fullfill requests
      master.foreach ([&](BBlock *b, const diy::Master::ProxyWithLink &cp) {

        std::map<int, datablock> stage;
          
        //   remote_enq_fulfill

        for (data_req &req: b->data_reqsts){

            datablock db;
            db.to_proc = req.from_proc;
            db.from_proc = rank;
            // iterage over each proc's cids  
            for (int &cid : req.cids){
                db.data.push_back(b->data[cid]);
                db.cgid.push_back(cid);
                db.bounds.push_back(b->bounds[cid]);
            }
            remote_enq(b, cp, assigner, db);  
        }

        // for ( auto &pair : stage ) {
        //     remote_enq(b, cp, assigner, pair.second); 
        // }


      });

    // do rexchange
    master.exchange(remote);
    master.foreach(&remote_deq_fulfill);



}


void update_mesh_data(BBlock *b){

    // b->mesh_data.numGlobalPoints = 0; // remains same
    int nobj = b->data.size();
    b->mesh_data.numMyPoints = nobj;
    
    // clean up
    if(b->mesh_data.numMyPoints > 0){
        free(b->mesh_data.myGlobalIDs);
        free(b->mesh_data.x);
        free(b->mesh_data.y);
        free(b->mesh_data.z);
    }

    
    b->mesh_data.myGlobalIDs = (ZOLTAN_ID_TYPE *)malloc(sizeof(ZOLTAN_ID_TYPE) * nobj);
    b->mesh_data.x = (float *)malloc(sizeof(float) * nobj);
    b->mesh_data.y = (float *)malloc(sizeof(float) * nobj);
    b->mesh_data.z = (float *)malloc(sizeof(float) * nobj);
    
    std::map<int, std::vector<float>>::iterator it = b->data.begin();
    int i=0;
    while (it != b->data.end()){

        b->mesh_data.myGlobalIDs[i] = it->first;
        b->mesh_data.x[i] = it->second[i];
        b->mesh_data.y[i] = it->second[nobj+i];
        b->mesh_data.z[i] = it->second[2*nobj+i];

        i++;
        it++;
    }


}

