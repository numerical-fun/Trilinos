/*------------------------------------------------------------------------*/
/*                 Copyright 2014 Sandia Corporation.                     */
/*  Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive   */
/*  license for use of this work by or on behalf of the U.S. Government.  */
/*  Export of this program may require  license from the                  */
/*  United States Government.                                             */
/*------------------------------------------------------------------------*/

#include <stk_io/StkMeshIoBroker.hpp>

#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/Field.hpp>
#include <stk_mesh/base/Bucket.hpp>
#include <stk_mesh/base/GetBuckets.hpp>
#include <stk_mesh/base/Selector.hpp>
#include <stk_mesh/base/GetEntities.hpp>        // for get_selected_entities
#include <stk_mesh/base/CoordinateSystems.hpp>  // for Cartesian3d
#include <stk_mesh/base/FieldBLAS.hpp>

#include <stk_util/parallel/ParallelReduce.hpp>
#ifdef STK_HAS_MPI
#include "mpi.h"
#endif

#include <gtest/gtest.h>
#include <string>
#include <complex>
#include <vector>

#include <stdio.h>

TEST(FieldBLAS,omp)
{
    EXPECT_TRUE(true);
#if    defined(_OPENMP) &&  defined(OPEN_MP_ACTIVE_FIELDBLAS_HPP)
    printf("openmp is on  and active\n");
#elif  defined(_OPENMP) && !defined(OPEN_MP_ACTIVE_FIELDBLAS_HPP)
    printf("openmp is on  and unactive\n");
#elif !defined(_OPENMP) &&  defined(OPEN_MP_ACTIVE_FIELDBLAS_HPP)
    printf("openmp is off and active\n");
#else
    printf("openmp is off and unactive\n");
#endif
}

template<class A>
struct BLASFixture {

    A initial_value1;
    A initial_value2;
    unsigned int numEntitiesUniversal;
    unsigned int numEntitiesOwned;
    unsigned int numEntitiesMPI;
    stk::io::StkMeshIoBroker * stkMeshIoBroker;
    stk::mesh::BulkData  * stkMeshBulkData;
    stk::mesh::Field<A>  * field1;
    stk::mesh::FieldBase * fieldBase1;
    stk::mesh::Field<A>  * field2;
    stk::mesh::FieldBase * fieldBase2;

    stk::mesh::Part * pPartA;
    stk::mesh::Part * pPartB;
    unsigned int numPartAEntitiesOwned;
    unsigned int numPartBEntitiesOwned;
    unsigned int numPartAEntitiesMPI;
    unsigned int numPartBEntitiesMPI;

    BLASFixture(const A init1 ,const A init2, const int MeshSize);
    ~BLASFixture();

};

template<class A>
BLASFixture<A>::BLASFixture(const A init1,const A init2 = A(), int MeshSize_x = 0)
{
    initial_value1 = init1;
    initial_value2 = init2;

    MPI_Comm my_comm = MPI_COMM_WORLD;

    const double bucket_fraction_to_no_part = 1.2;
    const double bucket_fraction_to_part_A  = 1.1;
    const double bucket_fraction_to_part_B  = 0.2;

    int MeshSize = MeshSize_x;
    if (MeshSize==0) {
        int MPI_size = 1;
#ifdef STK_HAS_MPI
        if ( MPI_SUCCESS != MPI_Comm_size( my_comm , &MPI_size ) ) {
            MPI_size = 1 ;
        }
#endif
        double num_buckets_needed = bucket_fraction_to_no_part +
                bucket_fraction_to_part_A +
                bucket_fraction_to_part_B;
        MeshSize = int(pow(double(num_buckets_needed*512*MPI_size),1.0/3.0));
    }

    stkMeshIoBroker = new stk::io::StkMeshIoBroker(my_comm);
    stk::io::StkMeshIoBroker & io = *stkMeshIoBroker;
    char generatedMeshSpecification [25];
    sprintf(generatedMeshSpecification,"generated:%dx%dx%d",MeshSize,MeshSize,MeshSize);
    io.add_mesh_database(generatedMeshSpecification, stk::io::READ_MESH);
    io.create_input_mesh();
    stk::mesh::MetaData &meta_data = io.meta_data();

    field1 = &meta_data.declare_field<stk::mesh::Field<A> >(stk::topology::NODE_RANK, "field1");
    stk::mesh::put_field(*field1,field1->mesh_meta_data().universal_part(),&initial_value1);
    fieldBase1 = dynamic_cast<stk::mesh::FieldBase*>(field1);

    field2 = &meta_data.declare_field<stk::mesh::Field<A> >(stk::topology::NODE_RANK, "field2");
    stk::mesh::put_field(*field2,field2->mesh_meta_data().universal_part(),&initial_value2);
    fieldBase2 = dynamic_cast<stk::mesh::FieldBase*>(field2);

    io.populate_bulk_data();
    stkMeshBulkData = &io.bulk_data();

    const stk::mesh::Selector begin_all_selector = meta_data.universal_part() & stk::mesh::selectField(*field1);
    const stk::mesh::BucketVector & begin_buckets = stkMeshBulkData->get_buckets(field1->entity_rank(),begin_all_selector);

    numEntitiesUniversal=0;
    for (unsigned int i=0;i<begin_buckets.size();i++)
    {
        numEntitiesUniversal+=begin_buckets[i]->size();
    }
    unsigned int bucketCapacity = begin_buckets[0]->capacity();

    stk::mesh::Part & PartA = meta_data.declare_part( "PartA" , stk::topology::NODE_RANK );
    pPartA = &PartA;
    stk::mesh::PartVector PV_A = stk::mesh::PartVector();
    PV_A.push_back(&PartA);

    stk::mesh::Part & PartB = meta_data.declare_part( "PartB" , stk::topology::NODE_RANK );
    pPartB = &PartB;
    stk::mesh::PartVector PV_B = stk::mesh::PartVector();
    PV_B.push_back(&PartB);

    std::vector<stk::mesh::Entity> entities;
    stk::mesh::get_selected_entities(field1->mesh_meta_data().locally_owned_part() & (PartA|PartB).complement(),
                                     stkMeshBulkData->buckets(field1->entity_rank()), entities);
    unsigned int num_entities_to_part_A = bucket_fraction_to_part_A*bucketCapacity;
    EXPECT_GT(entities.size(),num_entities_to_part_A);

    stkMeshBulkData->modification_begin();
    for (unsigned int i=0; i<(unsigned int)(bucket_fraction_to_part_A*bucketCapacity); i++)
    {
        stkMeshBulkData->change_entity_parts(entities[i],PV_A);
    }
    stkMeshBulkData->modification_end();

    stk::mesh::get_selected_entities(field1->mesh_meta_data().locally_owned_part() & (PartA|PartB).complement(),
                                     stkMeshBulkData->buckets(field1->entity_rank()), entities);
    unsigned int num_entities_to_part_B = bucket_fraction_to_part_B*bucketCapacity;
    EXPECT_GT(entities.size(),num_entities_to_part_B);

    stkMeshBulkData->modification_begin();
    for (unsigned int i=0; i<(unsigned int)(bucket_fraction_to_part_B*bucketCapacity); i++)
    {
        stkMeshBulkData->change_entity_parts(entities[i],PV_B);
    }
    stkMeshBulkData->modification_end();

    const stk::mesh::Selector end_all_selector = meta_data.universal_part() & stk::mesh::selectField(*field1);
    const stk::mesh::BucketVector & end_buckets = stkMeshBulkData->get_buckets(field1->entity_rank(),end_all_selector);

    unsigned int numPartAEntitiesUniversal = 0;
    numPartAEntitiesOwned = 0;
    unsigned int numPartBEntitiesUniversal = 0;
    numPartBEntitiesOwned = 0;

    unsigned int numPartABEntities = 0;
    unsigned int numPartABcEntities = 0;
    unsigned int numEntitiesUniversal_end = 0;
    numEntitiesOwned = 0;
    unsigned int numBucketsOwned = 0;

    for (unsigned int i=0; i<end_buckets.size();i++)
    {
        numEntitiesUniversal_end += end_buckets[i]->size();
        if ((*end_buckets[i]).member(PartA)) {
            numPartAEntitiesUniversal += end_buckets[i]->size();
            if ((*end_buckets[i]).owned()) {
                numPartAEntitiesOwned += end_buckets[i]->size();
            }
        }
        if ((*end_buckets[i]).member(PartB)) {
            numPartBEntitiesUniversal += end_buckets[i]->size();
            if ((*end_buckets[i]).owned()) {
                numPartBEntitiesOwned += end_buckets[i]->size();
            }
        }
        if ((*end_buckets[i]).member(PartA) & (*end_buckets[i]).member(PartB)) {
            numPartABEntities += end_buckets[i]->size();
        }
        if (!((*end_buckets[i]).member(PartA) | (*end_buckets[i]).member(PartB))) {
            numPartABcEntities += end_buckets[i]->size();
        }
        if (end_buckets[i]->owned()) {
            numBucketsOwned += 1u;
            numEntitiesOwned += end_buckets[i]->size();
        }
    }
    EXPECT_EQ(numEntitiesUniversal,numEntitiesUniversal_end);
    EXPECT_EQ(numPartABEntities,0u);
    EXPECT_EQ(numPartAEntitiesUniversal+numPartBEntitiesUniversal+numPartABcEntities,numEntitiesUniversal);

    numEntitiesMPI = numEntitiesOwned;
    numPartAEntitiesMPI = numPartAEntitiesOwned;
    numPartBEntitiesMPI = numPartBEntitiesOwned;
#ifdef STK_HAS_MPI
    stk::all_reduce_sum(stkMeshBulkData->parallel(),&numEntitiesOwned     ,&numEntitiesMPI     ,1u);
    stk::all_reduce_sum(stkMeshBulkData->parallel(),&numPartAEntitiesOwned,&numPartAEntitiesMPI,1u);
    stk::all_reduce_sum(stkMeshBulkData->parallel(),&numPartBEntitiesOwned,&numPartBEntitiesMPI,1u);
#endif
    EXPECT_EQ(numEntitiesMPI,(unsigned int)pow(MeshSize+1,3));
    EXPECT_GT(numEntitiesMPI,bucketCapacity);
}

template<class A>
BLASFixture<A>::~BLASFixture() {
    delete stkMeshIoBroker;
}

template<class Scalar>
void testFieldValidation(BLASFixture<Scalar> & Fixture,Scalar val1,Scalar val2,double tol=1.0e-5) {
    const stk::mesh::Selector selector = Fixture.field1->mesh_meta_data().universal_part() & stk::mesh::selectField(*Fixture.field1);
    testFieldValidation(Fixture,val1,val2,selector,tol);
}

template<class Scalar>
void testFieldValidation(BLASFixture<Scalar> & Fixture,Scalar val1,Scalar val2,stk::mesh::Selector selector,double tol=1.0e-5) {
    const stk::mesh::BucketVector& buckets = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),selector);
    for(size_t j = 0; j < buckets.size(); j++)
    {
        const stk::mesh::Bucket& bucket = *buckets[j];
        for(size_t i=0; i<bucket.size(); i++)
        {
            Scalar* field_value1 = reinterpret_cast<Scalar *>(stk::mesh::field_data(*Fixture.field1, bucket[i]));
            EXPECT_NEAR(val1,*field_value1,tol);

            Scalar* field_value2 = reinterpret_cast<Scalar *>(stk::mesh::field_data(*Fixture.field2, bucket[i]));
            EXPECT_NEAR(val2,*field_value2,tol);
        }

    }
}

template<class Scalar>
void testFieldValidation(BLASFixture<std::complex<Scalar> > & Fixture,std::complex<Scalar> val1,std::complex<Scalar> val2,stk::mesh::Selector selector,double tol=1.0e-5) {
    const stk::mesh::BucketVector& buckets = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),selector);
    for(size_t j = 0; j < buckets.size(); j++)
    {
        const stk::mesh::Bucket& bucket = *buckets[j];
        for(size_t i=0; i<bucket.size(); i++)
        {
            std::complex<Scalar>* field_value1 = reinterpret_cast<std::complex<Scalar>* >(stk::mesh::field_data(*Fixture.field1, bucket[i]));
            EXPECT_LT(std::abs(val1-*field_value1),tol);

            std::complex<Scalar>* field_value2 = reinterpret_cast<std::complex<Scalar>* >(stk::mesh::field_data(*Fixture.field2, bucket[i]));
            EXPECT_LT(std::abs(val2-*field_value2),tol);
        }

    }
}

template<class Scalar>
void test_axpy(const Scalar alpha,const Scalar initial1,const Scalar initial2)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    stk::mesh::field_axpy(alpha,*Fixture.field1,*Fixture.field2);
    testFieldValidation(Fixture,initial1,alpha*initial1+initial2);
    stk::mesh::field_axpy(alpha,*Fixture.fieldBase1,*Fixture.fieldBase2);
    testFieldValidation(Fixture,initial1,alpha*initial1*Scalar(2)+initial2);
}

TEST(FieldBLAS,axpy_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;
    const double alpha    = 7.11;
    test_axpy(alpha,initial1,initial2);
}

TEST(FieldBLAS,axpy_float)
{
    const float initial1 = 1.2;
    const float initial2 = -3.1;
    const float alpha    = 4.1;
    test_axpy(alpha,initial1,initial2);
}

TEST(FieldBLAS,axpy_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.11,-7.63);
    const std::complex<double> initial2 = std::complex<double>(-7.21,-1.23);
    const std::complex<double> alpha    = std::complex<double>(-3.11,2.00);
    test_axpy(alpha,initial1,initial2);
}

TEST(FieldBLAS,axpy_int)
{
    const int initial1 = 4;
    const int initial2 = -3;
    const int alpha    = 7;
    test_axpy(alpha,initial1,initial2);
}

template<class Scalar>
void test_axpy_selector(Scalar initial1,Scalar initial2,Scalar alpha_1,Scalar alpha_2,Scalar alpha_all)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    stk::mesh::field_axpy(alpha_1,*Fixture.field1,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,alpha_1*initial1+initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_axpy(alpha_2,*Fixture.fieldBase1,*Fixture.fieldBase2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,alpha_1*initial1+initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,alpha_2*initial1+initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_axpy(alpha_all,*Fixture.field1,*Fixture.field2);
    testFieldValidation(Fixture,initial1,(alpha_1+alpha_all)*initial1+initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,(alpha_2+alpha_all)*initial1+initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,alpha_all*initial1+initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
}

TEST(FieldBLAS,axpy_selector_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;

    const double alpha_1   = 7.11;
    const double alpha_2   = 4.05;
    const double alpha_all = -2.04;

    test_axpy_selector(initial1,initial2,alpha_1,alpha_2,alpha_all);
}

TEST(FieldBLAS,axpy_selector_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;

    const float alpha_1   = 7.1;
    const float alpha_2   = 4.7;
    const float alpha_all = -2.3;

    test_axpy_selector(initial1,initial2,alpha_1,alpha_2,alpha_all);
}

TEST(FieldBLAS,axpy_selector_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.27,2.1);
    const std::complex<double> initial2 = std::complex<double>(-3.73,1.04);

    const std::complex<double> alpha_1   = std::complex<double>(7.11,-42.1);
    const std::complex<double> alpha_2   = std::complex<double>(4.05,7.22);
    const std::complex<double> alpha_all = std::complex<double>(-2.04,3.14);

    test_axpy_selector(initial1,initial2,alpha_1,alpha_2,alpha_all);
}

TEST(FieldBLAS,axpy_selector_int)
{
    const int initial1 = 4;
    const int initial2 = -3;

    const int alpha_1   = 7;
    const int alpha_2   = 5;
    const int alpha_all = -2;

    test_axpy_selector(initial1,initial2,alpha_1,alpha_2,alpha_all);
}

template<class Scalar>
void test_copy(const Scalar initial1,const Scalar initial2)
{
    BLASFixture<Scalar> Fixture1 (initial1,initial2);
    BLASFixture<Scalar> Fixture2 (initial1,initial2);

    stk::mesh::field_copy(*Fixture1.field1,*Fixture2.field2);
    stk::mesh::field_copy(*Fixture1.field2,*Fixture1.field1);

    testFieldValidation(Fixture1,initial2,initial2);
    testFieldValidation(Fixture2,initial1,initial1);

    stk::mesh::field_copy(*Fixture1.fieldBase1,*Fixture2.fieldBase2);
    stk::mesh::field_copy(*Fixture2.fieldBase1,*Fixture1.fieldBase2);

    testFieldValidation(Fixture1,initial2,initial1);
    testFieldValidation(Fixture2,initial1,initial2);
}

TEST(FieldBLAS,copy_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;
    test_copy(initial1,initial2);
}

TEST(FieldBLAS,copy_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;
    test_copy(initial1,initial2);
}

TEST(FieldBLAS,copy_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.11,-7.63);
    const std::complex<double> initial2 = std::complex<double>(-7.21,-1.23);
    test_copy(initial1,initial2);
}

TEST(FieldBLAS,copy_int)
{
    const int initial1 = 4;
    const int initial2 = -3;
    test_copy(initial1,initial2);
}

template<class Scalar>
void test_copy_selector(Scalar initial1,Scalar initial2)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    stk::mesh::field_copy(*Fixture.field1,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,initial1,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_copy(*Fixture.fieldBase2,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial1,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial2,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_copy(*Fixture.field1,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    testFieldValidation(Fixture,initial1,initial1,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial2,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
}

TEST(FieldBLAS,copy_selector_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;

    test_copy_selector(initial1,initial2);
}

TEST(FieldBLAS,copy_selector_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;

    test_copy_selector(initial1,initial2);
}

TEST(FieldBLAS,copy_selector_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.27,2.1);
    const std::complex<double> initial2 = std::complex<double>(-3.73,1.04);

    test_copy_selector(initial1,initial2);
}

TEST(FieldBLAS,copy_selector_int)
{
    const int initial1 = 4;
    const int initial2 = -3;

    test_copy_selector(initial1,initial2);
}

template<class Scalar>
void test_product(const Scalar initial1,const Scalar initial2)
{
    BLASFixture<Scalar> Fixture1 (initial1,initial2);
    BLASFixture<Scalar> Fixture2 (initial1,initial2);

    testFieldValidation(Fixture1,Scalar(pow(initial1,1))*Scalar(pow(initial2,0)),Scalar(pow(initial1,0))*Scalar(pow(initial2,1)),1.0e-1);
    testFieldValidation(Fixture2,Scalar(pow(initial1,1))*Scalar(pow(initial2,0)),Scalar(pow(initial1,0))*Scalar(pow(initial2,1)),1.0e-1);

    stk::mesh::field_product(*Fixture1.field1,*Fixture1.field2,*Fixture2.field1);

    testFieldValidation(Fixture1,Scalar(pow(initial1,1))*Scalar(pow(initial2,0)),Scalar(pow(initial1,0))*Scalar(pow(initial2,1)),1.0e-1);
    testFieldValidation(Fixture2,Scalar(pow(initial1,1))*Scalar(pow(initial2,1)),Scalar(pow(initial1,0))*Scalar(pow(initial2,1)),1.0e-1);

    stk::mesh::field_product(*Fixture1.fieldBase1,*Fixture2.fieldBase1,*Fixture2.fieldBase2);

    testFieldValidation(Fixture1,Scalar(pow(initial1,1))*Scalar(pow(initial2,0)),Scalar(pow(initial1,0))*Scalar(pow(initial2,1)),1.0e-1);
    testFieldValidation(Fixture2,Scalar(pow(initial1,1))*Scalar(pow(initial2,1)),Scalar(pow(initial1,2))*Scalar(pow(initial2,1)),1.0e-1);

    stk::mesh::field_product(*Fixture2.fieldBase1,*Fixture2.fieldBase1,*Fixture1.fieldBase2);

    testFieldValidation(Fixture1,Scalar(pow(initial1,1))*Scalar(pow(initial2,0)),Scalar(pow(initial1,2))*Scalar(pow(initial2,2)),1.0e-1);
    testFieldValidation(Fixture2,Scalar(pow(initial1,1))*Scalar(pow(initial2,1)),Scalar(pow(initial1,2))*Scalar(pow(initial2,1)),1.0e-1);

    stk::mesh::field_product(*Fixture1.field1,*Fixture1.field1,*Fixture1.field1);

    testFieldValidation(Fixture1,Scalar(pow(initial1,2))*Scalar(pow(initial2,0)),Scalar(pow(initial1,2))*Scalar(pow(initial2,2)),1.0e-1);
    testFieldValidation(Fixture2,Scalar(pow(initial1,1))*Scalar(pow(initial2,1)),Scalar(pow(initial1,2))*Scalar(pow(initial2,1)),1.0e-1);

    stk::mesh::field_product(*Fixture1.fieldBase1,*Fixture2.fieldBase1,*Fixture1.fieldBase2);

    testFieldValidation(Fixture1,Scalar(pow(initial1,2))*Scalar(pow(initial2,0)),Scalar(pow(initial1,3))*Scalar(pow(initial2,1)),1.0e-1);
    testFieldValidation(Fixture2,Scalar(pow(initial1,1))*Scalar(pow(initial2,1)),Scalar(pow(initial1,2))*Scalar(pow(initial2,1)),1.0e-1);
}

TEST(FieldBLAS,product_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;
    test_product(initial1,initial2);
}

TEST(FieldBLAS,product_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;
    test_product(initial1,initial2);
}

TEST(FieldBLAS,product_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.11,-7.63);
    const std::complex<double> initial2 = std::complex<double>(-7.21,-1.23);
    test_product(initial1,initial2);
}

TEST(FieldBLAS,product_int)
{
    const int initial1 = 4;
    const int initial2 = -3;
    test_product(initial1,initial2);
}

template<class Scalar>
void test_product_selector(Scalar initial1,Scalar initial2)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    stk::mesh::field_product(*Fixture.field1,*Fixture.field2,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,initial1*initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_product(*Fixture.fieldBase2,*Fixture.fieldBase1,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial1*initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1*initial2,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_product(*Fixture.field1,*Fixture.field2,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    stk::mesh::field_product(*Fixture.field1,*Fixture.field2,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    testFieldValidation(Fixture,initial1,initial1*initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1*initial2,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,Scalar(pow(initial1,2))*initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
}

TEST(FieldBLAS,product_selector_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;

    test_product_selector(initial1,initial2);
}

TEST(FieldBLAS,product_selector_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;

    test_product_selector(initial1,initial2);
}

TEST(FieldBLAS,product_selector_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.27,2.1);
    const std::complex<double> initial2 = std::complex<double>(-3.73,1.04);

    test_product_selector(initial1,initial2);
}

TEST(FieldBLAS,product_selector_int)
{
    const int initial1 = 4;
    const int initial2 = -3;

    test_product_selector(initial1,initial2);
}

template<class Scalar>
void test_dot(const Scalar initial1,const Scalar initial2,const double TOL = 0.5)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    Scalar field_result = stk::mesh::field_dot(*Fixture.field1,*Fixture.field2);
    EXPECT_NEAR(field_result,initial1*initial2*Scalar(Fixture.numEntitiesMPI),TOL);

    Scalar fieldBase_result;
    stk::mesh::field_dot(fieldBase_result,*Fixture.fieldBase1,*Fixture.fieldBase2);
    EXPECT_NEAR(fieldBase_result,initial1*initial2*Scalar(Fixture.numEntitiesMPI),TOL);
}

template<class Scalar>
void test_dot(const std::complex<Scalar> initial1,const std::complex<Scalar> initial2,const double TOL = 1.0e-1)
{
    BLASFixture<std::complex<Scalar> > Fixture (initial1,initial2);

    std::complex<Scalar> field_result = stk::mesh::field_dot(*Fixture.field1,*Fixture.field2);
    EXPECT_LT(std::abs(field_result-initial1*initial2*Scalar(Fixture.numEntitiesMPI)),TOL);
    std::complex<Scalar> fieldBase_result;
    stk::mesh::field_dot(fieldBase_result,*Fixture.fieldBase1,*Fixture.fieldBase2);
    EXPECT_LT(std::abs(fieldBase_result-initial1*initial2*Scalar(Fixture.numEntitiesMPI)),TOL);
}

TEST(FieldBLAS,dot_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;
    test_dot(initial1,initial2);
}

TEST(FieldBLAS,dot_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;
    test_dot(initial1,initial2);
}

TEST(FieldBLAS,dot_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.11,-7.63);
    const std::complex<double> initial2 = std::complex<double>(-7.21,-1.23);
    test_dot(initial1,initial2);
}

TEST(FieldBLAS,dot_int)
{
    const int initial1 = 4;
    const int initial2 = -3;
    test_dot(initial1,initial2);
}

template<class Scalar>
void test_dot_selector(Scalar initial1,Scalar initial2,const double TOL = 1.0e-1)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    Scalar resultA=stk::mesh::field_dot(*Fixture.field1,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_NEAR(initial1*initial2*Scalar(Fixture.numPartAEntitiesMPI),resultA,TOL);

    Scalar resultB;
    stk::mesh::field_dot(resultB,*Fixture.fieldBase2,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_NEAR(initial1*initial2*Scalar(Fixture.numPartBEntitiesMPI),resultB,TOL);

    Scalar resultABc=stk::mesh::field_dot(*Fixture.field1,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_NEAR(initial1*initial2*Scalar(Fixture.numEntitiesMPI-Fixture.numPartAEntitiesMPI-Fixture.numPartBEntitiesMPI),resultABc,TOL);
}

template<class Scalar>
void test_dot_selector(std::complex<Scalar> initial1,std::complex<Scalar> initial2,const double TOL = 1.0e-1)
{
    BLASFixture<std::complex<Scalar> > Fixture (initial1,initial2);

    std::complex<Scalar> resultA=stk::mesh::field_dot(*Fixture.field1,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_LT(std::abs(initial1*initial2*Scalar(Fixture.numPartAEntitiesMPI)-resultA),TOL);

    std::complex<Scalar> resultB;
    stk::mesh::field_dot(resultB,*Fixture.fieldBase2,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_LT(std::abs(initial1*initial2*Scalar(Fixture.numPartBEntitiesMPI)-resultB),TOL);

    std::complex<Scalar> resultABc=stk::mesh::field_dot(*Fixture.field1,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_LT(std::abs(initial1*initial2*Scalar(Fixture.numEntitiesMPI-Fixture.numPartAEntitiesMPI-Fixture.numPartBEntitiesMPI)-resultABc),TOL);
}

TEST(FieldBLAS,dot_selector_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;

    test_dot_selector(initial1,initial2);
}

TEST(FieldBLAS,dot_selector_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;

    test_dot_selector(initial1,initial2);
}

TEST(FieldBLAS,dot_selector_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.27,2.1);
    const std::complex<double> initial2 = std::complex<double>(-3.73,1.04);

    test_dot_selector(initial1,initial2);
}

TEST(FieldBLAS,dot_selector_int)
{
    const int initial1 = 4;
    const int initial2 = -3;

    test_dot_selector(initial1,initial2);
}

template<class Scalar>
void test_scale(const Scalar alpha,const Scalar initial1)
{
    BLASFixture<Scalar> Fixture (initial1,initial1);

    stk::mesh::field_scale(alpha,*Fixture.field1);
    stk::mesh::field_scale(alpha,*Fixture.fieldBase2);
    testFieldValidation(Fixture,alpha*initial1,alpha*initial1);
}

TEST(FieldBLAS,scale_double)
{
    const double alpha = 4.27;
    const double initial1 = -3.73;
    test_scale(alpha,initial1);
}

TEST(FieldBLAS,scale_float)
{
    const float alpha = 4.2;
    const float initial1 = -3.7;
    test_scale(alpha,initial1);
}

TEST(FieldBLAS,scale_complex)
{
    const std::complex<double> alpha = std::complex<double>(4.11,-7.63);
    const std::complex<double> initial1 = std::complex<double>(-7.21,-1.23);
    test_scale(alpha,initial1);
}

TEST(FieldBLAS,scale_int)
{
    const int alpha = 4;
    const int initial1 = -3;
    test_scale(alpha,initial1);
}

template<class Scalar>
void test_scale_selector(Scalar alpha,Scalar initial1,Scalar initial2)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    stk::mesh::field_scale(alpha,*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,alpha*initial1,initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_scale(alpha,*Fixture.fieldBase2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,alpha*initial1,initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,alpha*initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_scale(alpha,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    testFieldValidation(Fixture,alpha*initial1,initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,alpha*initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,alpha*initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
}

TEST(FieldBLAS,scale_selector_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;

    const double alpha = 2.13;

    test_scale_selector(alpha,initial1,initial2);
}

TEST(FieldBLAS,scale_selector_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;

    const float alpha = 7.21;

    test_scale_selector(alpha,initial1,initial2);
}

TEST(FieldBLAS,scale_selector_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.27,2.1);
    const std::complex<double> initial2 = std::complex<double>(-3.73,1.04);

    const std::complex<double> alpha = -4.2;

    test_scale_selector(alpha,initial1,initial2);
}

TEST(FieldBLAS,scale_selector_int)
{
    const int initial1 = 4;
    const int initial2 = -3;

    const int alpha = 2;

    test_scale_selector(alpha,initial1,initial2);
}

template<class Scalar>
void test_fill(const Scalar alpha,const Scalar initial1)
{
    BLASFixture<Scalar> Fixture (initial1,initial1);

    stk::mesh::field_fill(alpha,*Fixture.field1);
    stk::mesh::field_fill(alpha,*Fixture.fieldBase2);
    testFieldValidation(Fixture,alpha,alpha);
}

TEST(FieldBLAS,fill_double)
{
    const double alpha = 4.27;
    const double initial1 = -3.73;
    test_fill(alpha,initial1);
}

TEST(FieldBLAS,fill_float)
{
    const float alpha = 4.2;
    const float initial1 = -3.7;
    test_fill(alpha,initial1);
}

TEST(FieldBLAS,fill_complex)
{
    const std::complex<double> alpha = std::complex<double>(4.11,-7.63);
    const std::complex<double> initial1 = std::complex<double>(-7.21,-1.23);
    test_fill(alpha,initial1);
}

TEST(FieldBLAS,fill_int)
{
    const int alpha = 4;
    const int initial1 = -3;
    test_fill(alpha,initial1);
}

template<class Scalar>
void test_fill_selector(Scalar alpha,Scalar initial1,Scalar initial2)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    stk::mesh::field_fill(alpha,*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,alpha,initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_fill(alpha,*Fixture.fieldBase2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,alpha,initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,alpha,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_fill(Scalar(0.0),*Fixture.fieldBase1);
    testFieldValidation(Fixture,Scalar(0.0),initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,Scalar(0.0),alpha,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,Scalar(0.0),initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_fill(alpha,*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    testFieldValidation(Fixture,Scalar(0.0),initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,Scalar(0.0),alpha,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,alpha,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
}

TEST(FieldBLAS,fill_selector_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;

    const double alpha = 2.13;

    test_fill_selector(alpha,initial1,initial2);
}

TEST(FieldBLAS,fill_selector_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;

    const float alpha = 7.21;

    test_fill_selector(alpha,initial1,initial2);
}

TEST(FieldBLAS,fill_selector_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.27,2.1);
    const std::complex<double> initial2 = std::complex<double>(-3.73,1.04);

    const std::complex<double> alpha = -4.2;

    test_fill_selector(alpha,initial1,initial2);
}

TEST(FieldBLAS,fill_selector_int)
{
    const int initial1 = 4;
    const int initial2 = -3;
    const int alpha = 2;

    test_fill_selector(alpha,initial1,initial2);
}

template<class Scalar>
void test_swap(const Scalar initial1,const Scalar initial2)
{
    BLASFixture<Scalar> Fixture1 (initial1,initial2);
    BLASFixture<Scalar> Fixture2 (initial1,initial2);

    stk::mesh::field_swap(*Fixture1.field1,*Fixture2.field2);
    stk::mesh::field_swap(*Fixture1.field2,*Fixture2.field1);

    testFieldValidation(Fixture1,initial2,initial1);
    testFieldValidation(Fixture2,initial2,initial1);

    stk::mesh::field_swap(*Fixture1.fieldBase1,*Fixture2.fieldBase2);
    stk::mesh::field_swap(*Fixture1.fieldBase2,*Fixture2.fieldBase1);

    testFieldValidation(Fixture1,initial1,initial2);
    testFieldValidation(Fixture2,initial1,initial2);
}

TEST(FieldBLAS,swap_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;
    test_swap(initial1,initial2);
}

TEST(FieldBLAS,swap_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;
    test_swap(initial1,initial2);
}

TEST(FieldBLAS,swap_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.11,-7.63);
    const std::complex<double> initial2 = std::complex<double>(-7.21,-1.23);
    test_swap(initial1,initial2);
}

TEST(FieldBLAS,swap_int)
{
    const int initial1 = 4;
    const int initial2 = -3;
    test_swap(initial1,initial2);
}

template<class Scalar>
void test_swap_selector(Scalar initial1,Scalar initial2)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    stk::mesh::field_swap(*Fixture.field1,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial2,initial1,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_swap(*Fixture.fieldBase2,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial2,initial1,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial2,initial1,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_swap(*Fixture.fieldBase2,*Fixture.fieldBase1);
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial2,initial1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());

    stk::mesh::field_swap(*Fixture.field1,*Fixture.field2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartB));
    testFieldValidation(Fixture,initial1,initial2,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
}

TEST(FieldBLAS,swap_selector_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;

    test_swap_selector(initial1,initial2);
}

TEST(FieldBLAS,swap_selector_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;

    test_swap_selector(initial1,initial2);
}

TEST(FieldBLAS,swap_selector_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.27,2.1);
    const std::complex<double> initial2 = std::complex<double>(-3.73,1.04);

    test_swap_selector(initial1,initial2);
}

TEST(FieldBLAS,swap_selector_int)
{
    const int initial1 = 4;
    const int initial2 = -3;

    test_swap_selector(initial1,initial2);
}

template<class Scalar>
void test_nrm2(const Scalar initial1,const Scalar initial2,const double TOL = 1.0e-1)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    Scalar field_result1 = stk::mesh::field_nrm2(*Fixture.field1);
    EXPECT_LT(std::abs(field_result1-Scalar(sqrt(std::abs(initial1)*std::abs(initial1)*double(Fixture.numEntitiesMPI)))),TOL);
    Scalar field_result2 = stk::mesh::field_nrm2(*Fixture.field2);
    EXPECT_LT(std::abs(field_result2-Scalar(sqrt(std::abs(initial2)*std::abs(initial2)*double(Fixture.numEntitiesMPI)))),TOL);

    Scalar fieldBase_result1;
    stk::mesh::field_nrm2(fieldBase_result1,*Fixture.fieldBase1);
    EXPECT_LT(std::abs(fieldBase_result1-Scalar(sqrt(std::abs(initial1)*std::abs(initial1)*double(Fixture.numEntitiesMPI)))),TOL);
    Scalar fieldBase_result2;
    stk::mesh::field_nrm2(fieldBase_result2,*Fixture.fieldBase2);
    EXPECT_LT(std::abs(fieldBase_result2-Scalar(sqrt(std::abs(initial2)*std::abs(initial2)*double(Fixture.numEntitiesMPI)))),TOL);
}

TEST(FieldBLAS,nrm2_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;
    test_nrm2(initial1,initial2);
}

TEST(FieldBLAS,nrm2_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;
    test_nrm2(initial1,initial2);
}

TEST(FieldBLAS,nrm2_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.11,-7.63);
    const std::complex<double> initial2 = std::complex<double>(-7.21,-1.23);
    test_nrm2(initial1,initial2);
}

TEST(FieldBLAS,nrm2_int)
{
    const int initial1 = 4;
    const int initial2 = -3;
    test_nrm2(initial1,initial2,5);
}

template<class Scalar>
void test_nrm2_selector(Scalar initial1,Scalar initial2,const double TOL = 1.0e-1)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    Scalar resultA=stk::mesh::field_nrm2(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_LT(std::abs(Scalar(std::abs(initial1)*sqrt(Scalar(Fixture.numPartAEntitiesMPI)))-resultA),TOL);

    Scalar resultB;
    stk::mesh::field_nrm2(resultB,*Fixture.fieldBase2,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_LT(std::abs(Scalar(std::abs(initial2)*sqrt(Scalar(Fixture.numPartBEntitiesMPI)))-resultB),TOL);

    Scalar resultABc=stk::mesh::field_nrm2(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_LT(std::abs(Scalar(std::abs(initial1)*sqrt(Scalar(Fixture.numEntitiesMPI-Fixture.numPartAEntitiesMPI-Fixture.numPartBEntitiesMPI)))-resultABc),TOL);
}

TEST(FieldBLAS,nrm2_selector_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;

    test_nrm2_selector(initial1,initial2);
}

TEST(FieldBLAS,nrm2_selector_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;

    test_nrm2_selector(initial1,initial2);
}

TEST(FieldBLAS,nrm2_selector_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.27,2.1);
    const std::complex<double> initial2 = std::complex<double>(-3.73,1.04);

    test_nrm2_selector(initial1,initial2);
}

TEST(FieldBLAS,nrm2_selector_int)
{
    const int initial1 = 4;
    const int initial2 = -3;

    test_nrm2_selector(initial1,initial2,5);
}

template<class Scalar>
void test_asum(const Scalar initial1,const Scalar initial2,const double TOL = 1.0e-1)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    Scalar field_result1 = stk::mesh::field_asum(*Fixture.field1);
    EXPECT_LT(std::abs(field_result1-std::abs(initial1)*Scalar(Fixture.numEntitiesMPI)),TOL);
    Scalar field_result2 = stk::mesh::field_asum(*Fixture.field2);
    EXPECT_LT(std::abs(field_result2-std::abs(initial2)*Scalar(Fixture.numEntitiesMPI)),TOL);

    Scalar fieldBase_result1;
    stk::mesh::field_asum(fieldBase_result1,*Fixture.fieldBase1);
    EXPECT_LT(std::abs(fieldBase_result1-std::abs(initial1)*Scalar(Fixture.numEntitiesMPI)),TOL);
    Scalar fieldBase_result2;
    stk::mesh::field_asum(fieldBase_result2,*Fixture.fieldBase2);
    EXPECT_LT(std::abs(fieldBase_result2-std::abs(initial2)*Scalar(Fixture.numEntitiesMPI)),TOL);
}

TEST(FieldBLAS,asum_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;
    test_asum(initial1,initial2);
}

TEST(FieldBLAS,asum_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;
    test_asum(initial1,initial2);
}

TEST(FieldBLAS,asum_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.11,-7.63);
    const std::complex<double> initial2 = std::complex<double>(-7.21,-1.23);
    test_asum(initial1,initial2);
}

TEST(FieldBLAS,asum_int)
{
    const int initial1 = 4;
    const int initial2 = -3;
    test_asum(initial1,initial2);
}

template<class Scalar>
void test_asum_selector(Scalar initial1,Scalar initial2,const double TOL = 1.0e-1)
{
    BLASFixture<Scalar> Fixture (initial1,initial2);

    Scalar resultA=stk::mesh::field_asum(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_LT(std::abs(std::abs(initial1)*Scalar(Fixture.numPartAEntitiesMPI)-resultA),TOL);

    Scalar resultB;
    stk::mesh::field_asum(resultB,*Fixture.fieldBase2,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_LT(std::abs(std::abs(initial2)*Scalar(Fixture.numPartBEntitiesMPI)-resultB),TOL);

    Scalar resultABc=stk::mesh::field_asum(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_LT(std::abs(std::abs(initial1)*Scalar(Fixture.numEntitiesMPI-Fixture.numPartAEntitiesMPI-Fixture.numPartBEntitiesMPI)-resultABc),TOL);
}

TEST(FieldBLAS,asum_selector_double)
{
    const double initial1 = 4.27;
    const double initial2 = -3.73;

    test_asum_selector(initial1,initial2);
}

TEST(FieldBLAS,asum_selector_float)
{
    const float initial1 = 4.2;
    const float initial2 = -3.7;

    test_asum_selector(initial1,initial2);
}

TEST(FieldBLAS,asum_selector_complex)
{
    const std::complex<double> initial1 = std::complex<double>(4.27,2.1);
    const std::complex<double> initial2 = std::complex<double>(-3.73,1.04);

    test_asum_selector(initial1,initial2);
}

TEST(FieldBLAS,asum_selector_int)
{
    const int initial1 = 4;
    const int initial2 = -3;

    test_asum_selector(initial1,initial2);
}

template<class Scalar>
void test_amax(Scalar low_val,Scalar high_val)
{
    BLASFixture<Scalar> Fixture (low_val,low_val);

    const stk::mesh::MetaData &metaData = Fixture.stkMeshBulkData->mesh_meta_data();
    const stk::mesh::BucketVector buckets = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                 metaData.universal_part() &
                                                                                 stk::mesh::selectField(*Fixture.field1) &
                                                                                 metaData.locally_owned_part());
    double MPI_frac = 1.0;
#ifdef STK_HAS_MPI
    MPI_frac = double(stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel())+1)/double(stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel()));
#endif
    stk::mesh::Bucket & b = *buckets[buckets.size()/3u];
    Scalar * x = (Scalar*)stk::mesh::field_data(*Fixture.field1, b);
    x[b.size()/3u]=high_val*std::abs(low_val/high_val)+Scalar(high_val-high_val*std::abs(low_val/high_val))*MPI_frac;

    Scalar field_result = stk::mesh::field_amax(*Fixture.field1);
    EXPECT_EQ(field_result,std::abs(high_val));

    Scalar fieldBase_result;
    stk::mesh::field_amax(fieldBase_result,*Fixture.fieldBase1);
    EXPECT_EQ(fieldBase_result,std::abs(high_val));
}

TEST(FieldBLAS,amax_double)
{
    const double low_val  = 2.73;
    const double high_val = -10.27;
    test_amax(low_val,high_val);
}

TEST(FieldBLAS,amax_float)
{
    const float low_val  = 3.7;
    const float high_val = -10.2;
    test_amax(low_val,high_val);
}

TEST(FieldBLAS,amax_complex)
{
    const std::complex<double> low_val  = std::complex<double>(-1.11,-2.63);
    const std::complex<double> high_val = std::complex<double>(-100.21,-250.23);
    test_amax(low_val,high_val);
}

TEST(FieldBLAS,amax_int)
{
    const int low_val  = 2;
    const int high_val = -10;
    test_amax(low_val,high_val);
}

template<class Scalar>
void test_amax_selector(Scalar low_value,Scalar high_valueA,Scalar high_valueAB,Scalar high_valueABc,const double TOL = 1.0e-1)
{
    BLASFixture<Scalar> Fixture (low_value,low_value);

    const stk::mesh::MetaData &metaData = Fixture.stkMeshBulkData->mesh_meta_data();
    double MPI_frac = 1.0;
#ifdef STK_HAS_MPI
    MPI_frac = double(stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel())+1)/double(stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel()));
#endif

    Scalar tmp_result;

    const stk::mesh::BucketVector bucketsA = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                  stk::mesh::Selector(*Fixture.pPartA) &
                                                                                  metaData.locally_owned_part());
    stk::mesh::Bucket & bA = *bucketsA[bucketsA.size()/3];
    Scalar* xA = (Scalar*)stk::mesh::field_data(*Fixture.field1, bA);
    xA[bA.size()/3u]=high_valueA*std::abs(low_value/high_valueA)+Scalar(high_valueA-high_valueA*std::abs(low_value/high_valueA))*MPI_frac;

    stk::mesh::field_amax(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_LT(std::abs(std::abs(high_valueA)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(low_value)-stk::mesh::field_amax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartB))),TOL);
    stk::mesh::field_amax(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_LT(std::abs(std::abs(high_valueA)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(low_value)-stk::mesh::field_amax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement())),TOL);
    stk::mesh::field_amax(tmp_result,*Fixture.fieldBase1);
    EXPECT_LT(std::abs(std::abs(high_valueA)-tmp_result),TOL);

    const stk::mesh::BucketVector bucketsB = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                  stk::mesh::Selector(*Fixture.pPartB) &
                                                                                  metaData.locally_owned_part());
    stk::mesh::Bucket & bB = *bucketsB[bucketsB.size()/3];
    Scalar* xB = (Scalar*)stk::mesh::field_data(*Fixture.field1, bB);
    xB[bB.size()/3u]=high_valueAB*std::abs(low_value/high_valueAB)+Scalar(high_valueAB-high_valueAB*std::abs(low_value/high_valueAB))*MPI_frac;

    EXPECT_LT(std::abs(std::abs(high_valueA)-stk::mesh::field_amax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA))),TOL);
    stk::mesh::field_amax(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_LT(std::abs(std::abs(high_valueAB)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(high_valueAB)-stk::mesh::field_amax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB))),TOL);
    stk::mesh::field_amax(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_LT(std::abs(std::abs(low_value)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(high_valueAB)-stk::mesh::field_amax(*Fixture.field1)),TOL);

    const stk::mesh::BucketVector bucketsABc = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                    stk::mesh::Selector(*Fixture.pPartA).complement() &
                                                                                    stk::mesh::Selector(*Fixture.pPartB).complement() &
                                                                                    metaData.locally_owned_part());
    stk::mesh::Bucket & bABc = *bucketsABc[bucketsABc.size()/3];
    Scalar* xABc = (Scalar*)stk::mesh::field_data(*Fixture.field1, bABc);
    xABc[bABc.size()/3u]=high_valueABc*std::abs(low_value/high_valueABc)+Scalar(high_valueABc-high_valueABc*std::abs(low_value/high_valueABc))*MPI_frac;

    stk::mesh::field_amax(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_LT(std::abs(std::abs(high_valueA)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(high_valueAB)-stk::mesh::field_amax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartB))),TOL);
    stk::mesh::field_amax(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_LT(std::abs(std::abs(high_valueAB)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(high_valueABc)-stk::mesh::field_amax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement())),TOL);
    stk::mesh::field_amax(tmp_result,*Fixture.fieldBase1);
    EXPECT_LT(std::abs(std::abs(high_valueABc)-tmp_result),TOL);
}

TEST(FieldBLAS,amax_selector_double)
{
    const double low_value     = 1.27;
    const double high_valueA   = -3.73;
    const double high_valueAB  = -4.43;
    const double high_valueABc = -5.03;

    test_amax_selector(low_value,high_valueA,high_valueAB,high_valueABc);
}

TEST(FieldBLAS,amax_selector_float)
{
    const float low_value     = 1.3;
    const float high_valueA   = -3.7;
    const float high_valueAB  = -4.4;
    const float high_valueABc = -5.0;

    test_amax_selector(low_value,high_valueA,high_valueAB,high_valueABc);
}

TEST(FieldBLAS,amax_selector_complex)
{
    const std::complex<double> low_value     = std::complex<double>(0.51,0.32);
    const std::complex<double> high_valueA   = std::complex<double>(-3.73,4.04);
    const std::complex<double> high_valueAB  = std::complex<double>(4.95,-5.12);
    const std::complex<double> high_valueABc = std::complex<double>(-6.03,6.11);

    test_amax_selector(low_value,high_valueA,high_valueAB,high_valueABc);
}

TEST(FieldBLAS,amax_selector_int)
{
    const int low_value     = 1;
    const int high_valueA   = -3;
    const int high_valueAB  = -4;
    const int high_valueABc = -5;

    test_amax_selector(low_value,high_valueA,high_valueAB,high_valueABc);
}

template<class Scalar>
void test_eamax(Scalar low_val,Scalar high_val)
{
    BLASFixture<Scalar> Fixture (low_val,low_val);
    const stk::mesh::MetaData &metaData = Fixture.stkMeshBulkData->mesh_meta_data();
    const stk::mesh::BucketVector buckets = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                 metaData.universal_part() &
                                                                                 stk::mesh::selectField(*Fixture.field1) &
                                                                                 metaData.locally_owned_part());
    double MPI_frac = 1.0;
#ifdef STK_HAS_MPI
    MPI_frac = double(stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel())+1)/double(stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel()));
#endif
    stk::mesh::Bucket & b = *buckets[buckets.size()/3u];
    Scalar * x = (Scalar*)stk::mesh::field_data(*Fixture.field1, b);
    x[b.size()/3u]=high_val*std::abs(low_val/high_val)+Scalar(high_val-high_val*std::abs(low_val/high_val))*MPI_frac;

    stk::mesh::Entity field_result = stk::mesh::field_eamax(*Fixture.field1);
    EXPECT_EQ(MPI_frac==1.0,field_result.is_local_offset_valid());
    if (field_result.is_local_offset_valid()) EXPECT_EQ(*stk::mesh::field_data(*Fixture.field1,field_result),high_val);

    stk::mesh::Entity fieldBase_result=stk::mesh::field_eamax(*Fixture.fieldBase1);
    EXPECT_EQ(MPI_frac==1.0,fieldBase_result.is_local_offset_valid());
    if (fieldBase_result.is_local_offset_valid()) EXPECT_EQ(*stk::mesh::field_data(*Fixture.field1,fieldBase_result),high_val);
}

TEST(FieldBLAS,eamax_double)
{
    const double low_val  = 2.731;
    const double high_val = -10.27;
    test_eamax(low_val,high_val);
}

TEST(FieldBLAS,eamax_float)
{
    const float low_val  = 3.7;
    const float high_val = -10.2;
    test_eamax(low_val,high_val);
}

TEST(FieldBLAS,eamax_complex)
{
    const std::complex<double> low_val  = std::complex<double>(-1.11,-2.63);
    const std::complex<double> high_val = std::complex<double>(-100.21,-250.23);
    test_eamax(low_val,high_val);
}

TEST(FieldBLAS,eamax_int)
{
    const int low_val  = 2;
    const int high_val = -10;
    test_eamax(low_val,high_val);
}

template<class Scalar>
void test_eamax_selector(Scalar low_value,Scalar high_valueA,Scalar high_valueAB,Scalar high_valueABc,const double TOL = 1.0e-3)
{
    BLASFixture<Scalar> Fixture (low_value,low_value);

    const stk::mesh::MetaData &metaData = Fixture.stkMeshBulkData->mesh_meta_data();
    double MPI_frac = 1.0;
    bool MPI_is_last_proc = true;
#ifdef STK_HAS_MPI
    MPI_is_last_proc = (stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel())==stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel())-1);
    MPI_frac = double(stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel())+1)/double(stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel()));
#endif
    stk::mesh::Entity tmp_result;

    const stk::mesh::BucketVector bucketsA = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                  stk::mesh::Selector(*Fixture.pPartA) &
                                                                                  metaData.locally_owned_part());
    stk::mesh::Bucket & bA = *bucketsA[bucketsA.size()/3];
    Scalar* xA = (Scalar*)stk::mesh::field_data(*Fixture.field1, bA);
    xA[bA.size()/3u]=high_valueA*std::abs(low_value/high_valueA)+Scalar(high_valueA-high_valueA*std::abs(low_value/high_valueA))*MPI_frac;

    tmp_result = stk::mesh::field_eamax(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueA-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_is_last_proc,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_value-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueA-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_EQ(MPI_is_last_proc,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_value-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.fieldBase1);
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueA-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    const stk::mesh::BucketVector bucketsB = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                  stk::mesh::Selector(*Fixture.pPartB) &
                                                                                  metaData.locally_owned_part());
    stk::mesh::Bucket & bB = *bucketsB[bucketsB.size()/3];
    Scalar* xB = (Scalar*)stk::mesh::field_data(*Fixture.field1, bB);
    xB[bB.size()/3]=high_valueAB*std::abs(low_value/high_valueAB)+Scalar(high_valueAB-high_valueAB*std::abs(low_value/high_valueAB))*MPI_frac;

    tmp_result = stk::mesh::field_eamax(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueA-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueAB-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueAB-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_EQ(MPI_is_last_proc,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_value-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.fieldBase1);
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueAB-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    const stk::mesh::BucketVector bucketsABc = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                    stk::mesh::Selector(*Fixture.pPartA).complement() &
                                                                                    stk::mesh::Selector(*Fixture.pPartB).complement() &
                                                                                    metaData.locally_owned_part());
    stk::mesh::Bucket & bABc = *bucketsABc[bucketsABc.size()/3];
    Scalar* xABc = (Scalar*)stk::mesh::field_data(*Fixture.field1, bABc);
    xABc[bABc.size()/3]=high_valueABc*std::abs(low_value/high_valueABc)+Scalar(high_valueABc-high_valueABc*std::abs(low_value/high_valueABc))*MPI_frac;

    tmp_result = stk::mesh::field_eamax(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueA-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueAB-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueAB-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_EQ(MPI_is_last_proc,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueABc-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamax(*Fixture.fieldBase1);
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_valueABc-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);
}

TEST(FieldBLAS,eamax_selector_double)
{
    const double low_value     = 1.27;
    const double high_valueA   = -3.73;
    const double high_valueAB  = -4.43;
    const double high_valueABc = -5.03;

    test_eamax_selector(low_value,high_valueA,high_valueAB,high_valueABc);
}

TEST(FieldBLAS,eamax_selector_float)
{
    const float low_value     = 1.3;
    const float high_valueA   = -3.7;
    const float high_valueAB  = -4.4;
    const float high_valueABc = -5.0;

    test_eamax_selector(low_value,high_valueA,high_valueAB,high_valueABc);
}

TEST(FieldBLAS,eamax_selector_complex)
{
    const std::complex<double> low_value     = std::complex<double>(0.51,0.32);
    const std::complex<double> high_valueA   = std::complex<double>(-3.73,4.04);
    const std::complex<double> high_valueAB  = std::complex<double>(4.95,-5.12);
    const std::complex<double> high_valueABc = std::complex<double>(-6.03,6.11);

    test_eamax_selector(low_value,high_valueA,high_valueAB,high_valueABc);
}

TEST(FieldBLAS,eamax_selector_int)
{
    const int low_value     = 1;
    const int high_valueA   = -3;
    const int high_valueAB  = -4;
    const int high_valueABc = -5;

    test_eamax_selector(low_value,high_valueA,high_valueAB,high_valueABc);
}

template<class Scalar>
void test_amin(Scalar low_val,Scalar high_val,const double TOL = 1.0e-5)
{
    BLASFixture<Scalar> Fixture (high_val,high_val);
    const stk::mesh::MetaData &metaData = Fixture.stkMeshBulkData->mesh_meta_data();
    const stk::mesh::BucketVector buckets = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                 metaData.universal_part() &
                                                                                 stk::mesh::selectField(*Fixture.field1) &
                                                                                 metaData.locally_owned_part());
    double MPI_frac = 1.0;
#ifdef STK_HAS_MPI
    MPI_frac = double(stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel())+1)/double(stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel()));
#endif
    stk::mesh::Bucket & b = *buckets[buckets.size()/3u];
    Scalar * x = (Scalar*)stk::mesh::field_data(*Fixture.field1, b);
    x[b.size()/3u]=low_val*std::abs(high_val/low_val)+Scalar(low_val-low_val*std::abs(high_val/low_val))*MPI_frac;

    Scalar field_result = stk::mesh::field_amin(*Fixture.field1);
    printf("(%d of %d) field     : field_result     %f , set value %f , low_value %f\n",
           stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel()),
           stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel()),
           double(std::abs(field_result)),
           double(std::abs(x[b.size()/3u])),
           double(std::abs(low_val)));
    fflush(stdout);
    EXPECT_LT(std::abs(field_result-std::abs(low_val)),TOL);

    Scalar fieldBase_result;
    stk::mesh::field_amin(fieldBase_result,*Fixture.fieldBase1);
    printf("(%d of %d) fieldBase : fieldBase_result %f , set value %f , low_value %f\n",
           stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel()),
           stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel()),
           double(std::abs(fieldBase_result)),
           double(std::abs(x[b.size()/3u])),
           double(std::abs(low_val)));
    fflush(stdout);
    EXPECT_LT(std::abs(fieldBase_result-std::abs(low_val)),TOL);
}

TEST(FieldBLAS,amin_double)
{
    const double low_val  = 2.73;
    const double high_val = -10.27;
    test_amin(low_val,high_val);
}

TEST(FieldBLAS,amin_float)
{
    const float low_val  = 3.7;
    const float high_val = -10.2;
    test_amin(low_val,high_val);
}

TEST(FieldBLAS,amin_complex)
{
    const std::complex<double> low_val  = std::complex<double>(-1.11,-2.63);
    const std::complex<double> high_val = std::complex<double>(-100.21,-250.23);
    test_amin(low_val,high_val);
}

TEST(FieldBLAS,amin_int)
{
    const int low_val  = 2;
    const int high_val = -10;
    test_amin(low_val,high_val);
}

template<class Scalar>
void test_amin_selector(Scalar high_value,Scalar low_valueA,Scalar low_valueAB,Scalar low_valueABc,const double TOL = 1.0e-1)
{
    BLASFixture<Scalar> Fixture (high_value,high_value);

    const stk::mesh::MetaData &metaData = Fixture.stkMeshBulkData->mesh_meta_data();
    double MPI_frac = 1.0;
#ifdef STK_HAS_MPI
    MPI_frac = double(stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel())+1)/double(stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel()));
#endif

    Scalar tmp_result;

    const stk::mesh::BucketVector bucketsA = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                  stk::mesh::Selector(*Fixture.pPartA) &
                                                                                  metaData.locally_owned_part());
    stk::mesh::Bucket & bA = *bucketsA[bucketsA.size()/3];
    Scalar* xA = (Scalar*)stk::mesh::field_data(*Fixture.field1, bA);
    xA[bA.size()/3u]=low_valueA*std::abs(high_value/low_valueA)+Scalar(low_valueA-low_valueA*std::abs(high_value/low_valueA))*MPI_frac;

    stk::mesh::field_amin(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_LT(std::abs(std::abs(low_valueA)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(high_value)-stk::mesh::field_amin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartB))),TOL);
    stk::mesh::field_amin(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_LT(std::abs(std::abs(low_valueA)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(high_value)-stk::mesh::field_amin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement())),TOL);
    stk::mesh::field_amin(tmp_result,*Fixture.fieldBase1);
    EXPECT_LT(std::abs(std::abs(low_valueA)-tmp_result),TOL);

    const stk::mesh::BucketVector bucketsB = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                  stk::mesh::Selector(*Fixture.pPartB) &
                                                                                  metaData.locally_owned_part());
    stk::mesh::Bucket & bB = *bucketsB[bucketsB.size()/3];
    Scalar* xB = (Scalar*)stk::mesh::field_data(*Fixture.field1, bB);
    xB[bB.size()/3u]=low_valueAB*std::abs(high_value/low_valueAB)+Scalar(low_valueAB-low_valueAB*std::abs(high_value/low_valueAB))*MPI_frac;

    EXPECT_LT(std::abs(std::abs(low_valueA)-stk::mesh::field_amin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA))),TOL);
    stk::mesh::field_amin(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_LT(std::abs(std::abs(low_valueAB)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(low_valueAB)-stk::mesh::field_amin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB))),TOL);
    stk::mesh::field_amin(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_LT(std::abs(std::abs(high_value)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(low_valueAB)-stk::mesh::field_amin(*Fixture.field1)),TOL);

    const stk::mesh::BucketVector bucketsABc = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                    stk::mesh::Selector(*Fixture.pPartA).complement() &
                                                                                    stk::mesh::Selector(*Fixture.pPartB).complement() &
                                                                                    metaData.locally_owned_part());
    stk::mesh::Bucket & bABc = *bucketsABc[bucketsABc.size()/3];
    Scalar* xABc = (Scalar*)stk::mesh::field_data(*Fixture.field1, bABc);
    xABc[bABc.size()/3u]=low_valueABc*std::abs(high_value/low_valueABc)+Scalar(low_valueABc-low_valueABc*std::abs(high_value/low_valueABc))*MPI_frac;

    stk::mesh::field_amin(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_LT(std::abs(std::abs(low_valueA)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(low_valueAB)-stk::mesh::field_amin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartB))),TOL);
    stk::mesh::field_amin(tmp_result,*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_LT(std::abs(std::abs(low_valueAB)-tmp_result),TOL);
    EXPECT_LT(std::abs(std::abs(low_valueABc)-stk::mesh::field_amin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement())),TOL);
    stk::mesh::field_amin(tmp_result,*Fixture.fieldBase1);
    EXPECT_LT(std::abs(std::abs(low_valueABc)-tmp_result),TOL);
}

TEST(FieldBLAS,amin_selector_double)
{
    const double high_value   = -6.27;
    const double low_valueA   = 5.73;
    const double low_valueAB  = 4.43;
    const double low_valueABc = 3.03;

    test_amin_selector(high_value,low_valueA,low_valueAB,low_valueABc);
}

TEST(FieldBLAS,amin_selector_float)
{
    const float high_value   = -6.3;
    const float low_valueA   = 5.7;
    const float low_valueAB  = 4.4;
    const float low_valueABc = 3.1;

    test_amin_selector(high_value,low_valueA,low_valueAB,low_valueABc);
}

TEST(FieldBLAS,amin_selector_complex)
{
    const std::complex<double> high_value   = std::complex<double>(7.51,-8.32);
    const std::complex<double> low_valueA   = std::complex<double>(-6.73,6.04);
    const std::complex<double> low_valueAB  = std::complex<double>(4.95,-5.12);
    const std::complex<double> low_valueABc = std::complex<double>(-4.03,4.11);

    test_amin_selector(high_value,low_valueA,low_valueAB,low_valueABc);
}

TEST(FieldBLAS,amin_selector_int)
{
    const int high_value   = -6;
    const int low_valueA   = 5;
    const int low_valueAB  = 4;
    const int low_valueABc = 3;

    test_amin_selector(high_value,low_valueA,low_valueAB,low_valueABc);
}

template<class Scalar>
void test_eamin(Scalar low_val,Scalar high_val,const double TOL = 1.5e-4)
{
    BLASFixture<Scalar> Fixture (high_val,high_val);
    const stk::mesh::MetaData &metaData = Fixture.stkMeshBulkData->mesh_meta_data();
    const stk::mesh::BucketVector buckets = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                 metaData.universal_part() &
                                                                                 stk::mesh::selectField(*Fixture.field1) &
                                                                                 metaData.locally_owned_part());
    double MPI_frac = 1.0;
#ifdef STK_HAS_MPI
    MPI_frac = double(stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel())+1)/double(stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel()));
#endif
    stk::mesh::Bucket & b = *buckets[buckets.size()/3u];
    Scalar * x = (Scalar*)stk::mesh::field_data(*Fixture.field1, b);
    x[b.size()/3u]=low_val*std::abs(high_val/low_val)+Scalar(low_val-low_val*std::abs(high_val/low_val))*MPI_frac;

    stk::mesh::Entity field_result = stk::mesh::field_eamin(*Fixture.field1);
    EXPECT_EQ(MPI_frac==1.0,field_result.is_local_offset_valid());
    if (field_result.is_local_offset_valid()) EXPECT_LT(std::abs(*stk::mesh::field_data(*Fixture.field1,field_result)-low_val),TOL);

    stk::mesh::Entity fieldBase_result=stk::mesh::field_eamin(*Fixture.fieldBase1);
    EXPECT_EQ(MPI_frac==1.0,fieldBase_result.is_local_offset_valid());
    if (fieldBase_result.is_local_offset_valid()) EXPECT_LT(std::abs(*stk::mesh::field_data(*Fixture.field1,fieldBase_result)-low_val),TOL);
}

TEST(FieldBLAS,eamin_double)
{
    const double low_val  = 2.73;
    const double high_val = -10.27;
    test_eamin(low_val,high_val);
}

TEST(FieldBLAS,eamin_float)
{
    const float low_val  = 3.7;
    const float high_val = -10.2;
    test_eamin(low_val,high_val);
}

TEST(FieldBLAS,eamin_complex)
{
    const std::complex<double> low_val  = std::complex<double>(-1.11,-2.63);
    const std::complex<double> high_val = std::complex<double>(-100.21,-250.23);
    test_eamin(low_val,high_val);
}

TEST(FieldBLAS,eamin_int)
{
    const int low_val  = 2;
    const int high_val = -10;
    test_eamin(low_val,high_val);
}

template<class Scalar>
void test_eamin_selector(Scalar high_value,Scalar low_valueA,Scalar low_valueAB,Scalar low_valueABc,const double TOL = 1.0e-1)
{
    BLASFixture<Scalar> Fixture (high_value,high_value);

    const stk::mesh::MetaData &metaData = Fixture.stkMeshBulkData->mesh_meta_data();
    double MPI_frac = 1.0;
    bool MPI_is_last_proc = true;
#ifdef STK_HAS_MPI
    MPI_is_last_proc = (stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel())==stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel())-1);
    MPI_frac = double(stk::parallel_machine_rank(Fixture.stkMeshBulkData->parallel())+1)/double(stk::parallel_machine_size(Fixture.stkMeshBulkData->parallel()));
#endif
    stk::mesh::Entity tmp_result;

    const stk::mesh::BucketVector bucketsA = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                  stk::mesh::Selector(*Fixture.pPartA) &
                                                                                  metaData.locally_owned_part());
    stk::mesh::Bucket & bA = *bucketsA[bucketsA.size()/3];
    Scalar* xA = (Scalar*)stk::mesh::field_data(*Fixture.field1, bA);
    xA[bA.size()/3u]=low_valueA*std::abs(high_value/low_valueA)+Scalar(low_valueA-low_valueA*std::abs(high_value/low_valueA))*MPI_frac;

    tmp_result = stk::mesh::field_eamin(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueA-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_is_last_proc,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_value-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueA-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_EQ(MPI_is_last_proc,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_value-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.fieldBase1);
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueA-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    const stk::mesh::BucketVector bucketsB = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                  stk::mesh::Selector(*Fixture.pPartB) &
                                                                                  metaData.locally_owned_part());
    stk::mesh::Bucket & bB = *bucketsB[bucketsB.size()/3];
    Scalar* xB = (Scalar*)stk::mesh::field_data(*Fixture.field1, bB);
    xB[bB.size()/3]=low_valueAB*std::abs(high_value/low_valueAB)+Scalar(low_valueAB-low_valueAB*std::abs(high_value/low_valueAB))*MPI_frac;

    tmp_result = stk::mesh::field_eamin(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueA-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueAB-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueAB-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_EQ(MPI_is_last_proc,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(high_value-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.fieldBase1);
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueAB-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    const stk::mesh::BucketVector bucketsABc = Fixture.stkMeshBulkData->get_buckets(Fixture.field1->entity_rank(),
                                                                                    stk::mesh::Selector(*Fixture.pPartA).complement() &
                                                                                    stk::mesh::Selector(*Fixture.pPartB).complement() &
                                                                                    metaData.locally_owned_part());
    stk::mesh::Bucket & bABc = *bucketsABc[bucketsABc.size()/3];
    Scalar* xABc = (Scalar*)stk::mesh::field_data(*Fixture.field1, bABc);
    xABc[bABc.size()/3]=low_valueABc*std::abs(high_value/low_valueABc)+Scalar(low_valueABc-low_valueABc*std::abs(high_value/low_valueABc))*MPI_frac;

    tmp_result = stk::mesh::field_eamin(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueA-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueAB-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.fieldBase1,stk::mesh::Selector(*Fixture.pPartA)|stk::mesh::Selector(*Fixture.pPartB));
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueAB-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.field1,stk::mesh::Selector(*Fixture.pPartA).complement()&stk::mesh::Selector(*Fixture.pPartB).complement());
    EXPECT_EQ(MPI_is_last_proc,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueABc-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);

    tmp_result = stk::mesh::field_eamin(*Fixture.fieldBase1);
    EXPECT_EQ(MPI_frac==1.0,tmp_result.is_local_offset_valid());
    if (tmp_result.is_local_offset_valid()) EXPECT_LT(std::abs(low_valueABc-*stk::mesh::field_data(*Fixture.field1,tmp_result)),TOL);
}

TEST(FieldBLAS,eamin_selector_double)
{
    const double high_value   = -6.27;
    const double low_valueA   = 5.73;
    const double low_valueAB  = 4.43;
    const double low_valueABc = 3.03;

    test_eamin_selector(high_value,low_valueA,low_valueAB,low_valueABc);
}

TEST(FieldBLAS,eamin_selector_float)
{
    const float high_value   = -6.3;
    const float low_valueA   = 5.7;
    const float low_valueAB  = 4.4;
    const float low_valueABc = 3.1;

    test_eamin_selector(high_value,low_valueA,low_valueAB,low_valueABc);
}

TEST(FieldBLAS,eamin_selector_complex)
{
    const std::complex<double> high_value   = std::complex<double>(7.51,-8.32);
    const std::complex<double> low_valueA   = std::complex<double>(-6.73,6.04);
    const std::complex<double> low_valueAB  = std::complex<double>(4.95,-5.12);
    const std::complex<double> low_valueABc = std::complex<double>(-4.03,4.11);

    test_eamin_selector(high_value,low_valueA,low_valueAB,low_valueABc);
}

TEST(FieldBLAS,eamin_selector_int)
{
    const int high_value   = -6;
    const int low_valueA   = 5;
    const int low_valueAB  = 4;
    const int low_valueABc = 3;

    test_eamin_selector(high_value,low_valueA,low_valueAB,low_valueABc);
}

template<class A>
struct BLASFixture3d {

    A* init1;
    A* init2;
    A* init3;

    stk::io::StkMeshIoBroker * stkMeshIoBroker;
    stk::mesh::BulkData * stkMeshBulkData;
    stk::mesh::Field<A,stk::mesh::Cartesian3d> * field1;
    stk::mesh::FieldBase * fieldBase1;
    stk::mesh::Field<A,stk::mesh::Cartesian3d> * field2;
    stk::mesh::FieldBase * fieldBase2;
    stk::mesh::Field<A,stk::mesh::Cartesian3d> * field3;
    stk::mesh::FieldBase * fieldBase3;

    unsigned int numEntitiesOwned;
    unsigned int numEntitiesUniversal;
    unsigned int numEntitiesMPI;

    BLASFixture3d(A*,A*,A*,const int);
    ~BLASFixture3d();
};

template<class A>
BLASFixture3d<A>::BLASFixture3d(A* init1_input,A* init2_input,A* init3_input,const int MeshSize = 11)
{
    init1=init1_input;
    init2=init2_input;
    init3=init3_input;

    MPI_Comm my_comm = MPI_COMM_WORLD;
    stkMeshIoBroker = new stk::io::StkMeshIoBroker(my_comm);
    stk::io::StkMeshIoBroker & io = *stkMeshIoBroker;
    char generatedMeshSpecification [25];
    sprintf(generatedMeshSpecification,"generated:%dx%dx%d",MeshSize,MeshSize,MeshSize);
    io.add_mesh_database(generatedMeshSpecification, stk::io::READ_MESH);
    io.create_input_mesh();
    stk::mesh::MetaData &meta_data = io.meta_data();

    field1 = &meta_data.declare_field<stk::mesh::Field<A,stk::mesh::Cartesian3d> >(stk::topology::NODE_RANK, "field1");
    stk::mesh::put_field(*field1,field1->mesh_meta_data().universal_part(),init1);
    fieldBase1 = dynamic_cast<stk::mesh::FieldBase*>(field1);

    field2 = &meta_data.declare_field<stk::mesh::Field<A,stk::mesh::Cartesian3d> >(stk::topology::NODE_RANK, "field2");
    stk::mesh::put_field(*field2,field2->mesh_meta_data().universal_part(),init2);
    fieldBase2 = dynamic_cast<stk::mesh::FieldBase*>(field2);

    field3 = &meta_data.declare_field<stk::mesh::Field<A,stk::mesh::Cartesian3d> >(stk::topology::NODE_RANK, "field3");
    stk::mesh::put_field(*field3,field3->mesh_meta_data().universal_part(),init3);
    fieldBase3 = dynamic_cast<stk::mesh::FieldBase*>(field3);

    io.populate_bulk_data(); // THIS IS THE SLOW LINE
    stkMeshBulkData = &io.bulk_data();

    numEntitiesOwned    =0;
    numEntitiesUniversal=0;

    const stk::mesh::Selector end_all_selector = meta_data.universal_part() & stk::mesh::selectField(*field1);
    const stk::mesh::BucketVector & buckets = stkMeshBulkData->get_buckets(field1->entity_rank(),end_all_selector);
    for (unsigned int i=0; i<buckets.size();i++) {
        numEntitiesUniversal+=buckets[i]->size();
        if (buckets[i]->owned()) {
            numEntitiesOwned+=buckets[i]->size();
        }
    }

    numEntitiesMPI = numEntitiesOwned;
#ifdef STK_HAS_MPI
    stk::all_reduce_sum(stkMeshBulkData->parallel(),&numEntitiesOwned,&numEntitiesMPI,1u);
#endif
    EXPECT_EQ(numEntitiesMPI,(unsigned int)pow(MeshSize+1,3));
}

template<class A>
BLASFixture3d<A>::~BLASFixture3d() {
    delete stkMeshIoBroker;
}

template<class A,class T1,class T2,class T3,class T4,class T5,class T6,class T7>
bool test3dfield(const stk::mesh::Field<A,T1,T2,T3,T4,T5,T6,T7> & field,const A* expected_value,const double tol=1.5e-3)
{
    bool result=true;
    const stk::mesh::BucketVector& buckets_init = field.get_mesh().get_buckets(field.entity_rank(),field.mesh_meta_data().universal_part() & stk::mesh::selectField(field));
    for(size_t j = 0; j < buckets_init.size(); j++)
    {
        const stk::mesh::Bucket& bucket = *buckets_init[j];
        for(size_t i=0; i<bucket.size(); i++)
        {
            A* field_value = reinterpret_cast<A*>(stk::mesh::field_data(field,bucket[i]));
            if (result)
            {
                for (unsigned int k=0;k<3u;k++)
                {
                    EXPECT_NEAR(expected_value[k],field_value[k],tol);
                    if (std::abs(expected_value[k]-field_value[k])>tol) result=false;
                }
            }
        }
    }
    return result;
}

template<class A,class T1,class T2,class T3,class T4,class T5,class T6,class T7>
bool test3dfield(const stk::mesh::Field<std::complex<A>,T1,T2,T3,T4,T5,T6,T7> & field,const std::complex<A>* expected_value,const double tol=1.5e-3)
{
    bool result=true;
    const stk::mesh::BucketVector& buckets_init = field.get_mesh().get_buckets(field.entity_rank(),field.mesh_meta_data().universal_part() & stk::mesh::selectField(field));
    for(size_t j = 0; j < buckets_init.size(); j++)
    {
        const stk::mesh::Bucket& bucket = *buckets_init[j];
        for(size_t i=0; i<bucket.size(); i++)
        {
            std::complex<A>* field_value = reinterpret_cast<std::complex<A>*>(stk::mesh::field_data(field,bucket[i]));
            if (result)
            {
                for (unsigned int k=0;k<3u;k++)
                {

                    EXPECT_LT(std::abs(expected_value[k]-field_value[k]),tol);
                    if (std::abs(expected_value[k]-field_value[k])>tol) result=false;
                }
            }
        }
    }
    return result;
}

template<class Scalar>
void test_coordinate_axpy(BLASFixture3d<Scalar> &fixture,const Scalar alpha)
{
    Scalar result2 [3];
    for (int i=0;i<3;i++) result2[i]=fixture.init1[i]*alpha+fixture.init2[i];
    Scalar result3 [3];
    for (int i=0;i<3;i++) result3[i]=fixture.init1[i]*alpha+fixture.init3[i];

    stk::mesh::field_axpy(alpha,*fixture.field1,*fixture.field2);
    stk::mesh::field_axpy(alpha,*fixture.fieldBase1,*fixture.fieldBase3);

    EXPECT_TRUE(test3dfield(*fixture.field1,fixture.init1));
    EXPECT_TRUE(test3dfield(*fixture.field2,result2));
    EXPECT_TRUE(test3dfield(*fixture.field3,result3));
}


TEST(FieldBLAS,coordinate_axpy_double)
{
    const double alpha = 4.11;
    double init1 [3]   = {4.21,1.23,-2.13};
    double init2 [3]   = {1.32,4.17,11.27};
    double init3 [3]   = {0.24,-7.11,3.21};
    BLASFixture3d<double> fixture (init1,init2,init3);
    test_coordinate_axpy<double>(fixture,alpha);
}

TEST(FieldBLAS,coordinate_axpy_float)
{
    const float alpha = 4.1;
    float init1 [3]   = {4.2,1.2,-2.1};
    float init2 [3]   = {1.3,4.1,11.2};
    float init3 [3]   = {0.2,-7.1,3.2};
    BLASFixture3d<float> fixture (init1,init2,init3);
    test_coordinate_axpy<float>(fixture,alpha);
}

TEST(FieldBLAS,coordinate_axpy_complex)
{
    const std::complex<double> alpha = std::complex<double>(4.11,-32.1);
    std::complex<double>   init1 [3] = {std::complex<double>(4.21,0.24),std::complex<double>(1.23,-0.1),std::complex<double>(-2.13,4.11)};
    std::complex<double>   init2 [3] = {std::complex<double>(1.32,23.1),std::complex<double>(4.17,-0.2),std::complex<double>(11.27,4.21)};
    std::complex<double>   init3 [3] = {std::complex<double>(0.24,-1.22),std::complex<double>(-7.11,42.1),std::complex<double>(3.21,7.11)};
    BLASFixture3d<std::complex<double> > fixture (init1,init2,init3);

    test_coordinate_axpy<std::complex<double> >(fixture,alpha);
}

TEST(FieldBLAS,coordinate_axpy_int)
{
    const int alpha = 4;
    int init1 [3] = {4,1,-2};
    int init2 [3] = {3,4,11};
    int init3 [3] = {8,-7,3};
    BLASFixture3d<int> fixture (init1,init2,init3);

    test_coordinate_axpy<int>(fixture,alpha);
}

template<class Scalar>
void test_coordinate_copy(BLASFixture3d<Scalar> &fixture)
{
    stk::mesh::field_copy(*fixture.field1,*fixture.field2);
    stk::mesh::field_copy(*fixture.fieldBase3,*fixture.fieldBase1);

    EXPECT_TRUE(test3dfield(*fixture.field1,fixture.init3));
    EXPECT_TRUE(test3dfield(*fixture.field2,fixture.init1));
    EXPECT_TRUE(test3dfield(*fixture.field3,fixture.init3));
}


TEST(FieldBLAS,coordinate_copy_double)
{
    double init1 [3]   = {4.21,1.23,-2.13};
    double init2 [3]   = {1.32,4.17,11.27};
    double init3 [3]   = {0.24,-7.11,3.21};
    BLASFixture3d<double> fixture (init1,init2,init3);
    test_coordinate_copy<double>(fixture);
}

TEST(FieldBLAS,coordinate_copy_float)
{
    float init1 [3]   = {4.2,1.2,-2.1};
    float init2 [3]   = {1.3,4.1,11.2};
    float init3 [3]   = {0.2,-7.1,3.2};
    BLASFixture3d<float> fixture (init1,init2,init3);
    test_coordinate_copy<float>(fixture);
}

TEST(FieldBLAS,coordinate_copy_complex)
{
    std::complex<double>   init1 [3] = {std::complex<double>(4.21,0.24),std::complex<double>(1.23,-0.1),std::complex<double>(-2.13,4.11)};
    std::complex<double>   init2 [3] = {std::complex<double>(1.32,23.1),std::complex<double>(4.17,-0.2),std::complex<double>(11.27,4.21)};
    std::complex<double>   init3 [3] = {std::complex<double>(0.24,-1.22),std::complex<double>(-7.11,42.1),std::complex<double>(3.21,7.11)};
    BLASFixture3d<std::complex<double> > fixture (init1,init2,init3);

    test_coordinate_copy<std::complex<double> >(fixture);
}

TEST(FieldBLAS,coordinate_copy_int)
{
    int init1 [3] = {4,1,-2};
    int init2 [3] = {3,4,11};
    int init3 [3] = {8,-7,3};
    BLASFixture3d<int> fixture (init1,init2,init3);

    test_coordinate_copy<int>(fixture);
}

template<class Scalar>
void test_coordinate_product(BLASFixture3d<Scalar> &fixture)
{
    Scalar result3 [3];
    for (int i=0;i<3;i++) result3[i]=fixture.init1[i]*fixture.init2[i];
    Scalar result2 [3];
    for (int i=0;i<3;i++) result2[i]=fixture.init1[i]*result3[i];

    stk::mesh::field_product(*fixture.field1,*fixture.field2,*fixture.field3);
    stk::mesh::field_product(*fixture.fieldBase3,*fixture.fieldBase1,*fixture.fieldBase2);

    EXPECT_TRUE(test3dfield(*fixture.field1,fixture.init1));
    EXPECT_TRUE(test3dfield(*fixture.field2,result2));
    EXPECT_TRUE(test3dfield(*fixture.field3,result3));
}


TEST(FieldBLAS,coordinate_product_double)
{
    double init1 [3]   = {4.21,1.23,-2.13};
    double init2 [3]   = {1.32,4.17,11.27};
    double init3 [3]   = {0.24,-7.11,3.21};
    BLASFixture3d<double> fixture (init1,init2,init3);
    test_coordinate_product<double>(fixture);
}

TEST(FieldBLAS,coordinate_product_float)
{
    float init1 [3]   = {4.2,1.2,-2.1};
    float init2 [3]   = {1.3,4.1,11.2};
    float init3 [3]   = {0.2,-7.1,3.2};
    BLASFixture3d<float> fixture (init1,init2,init3);
    test_coordinate_product<float>(fixture);
}

TEST(FieldBLAS,coordinate_product_complex)
{
    std::complex<double>   init1 [3] = {std::complex<double>(4.21,0.24),std::complex<double>(1.23,-0.1),std::complex<double>(-2.13,4.11)};
    std::complex<double>   init2 [3] = {std::complex<double>(1.32,23.1),std::complex<double>(4.17,-0.2),std::complex<double>(11.27,4.21)};
    std::complex<double>   init3 [3] = {std::complex<double>(0.24,-1.22),std::complex<double>(-7.11,42.1),std::complex<double>(3.21,7.11)};
    BLASFixture3d<std::complex<double> > fixture (init1,init2,init3);

    test_coordinate_product<std::complex<double> >(fixture);
}

TEST(FieldBLAS,coordinate_product_int)
{
    int init1 [3] = {4,1,-2};
    int init2 [3] = {3,4,11};
    int init3 [3] = {8,-7,3};
    BLASFixture3d<int> fixture (init1,init2,init3);

    test_coordinate_product<int>(fixture);
}

template<class Scalar>
void test_coordinate_dot(BLASFixture3d<Scalar> &fixture,const double tol=1.5e-3)
{
    Scalar expected_result12=Scalar(0.0);
    for (int i=0;i<3;i++) expected_result12+=fixture.init1[i]*fixture.init2[i];
    Scalar expected_result23=Scalar(0.0);
    for (int i=0;i<3;i++) expected_result23+=fixture.init2[i]*fixture.init3[i];

    Scalar field_result = stk::mesh::field_dot(*fixture.field1,*fixture.field2);
    EXPECT_NEAR(field_result,expected_result12*Scalar(fixture.numEntitiesMPI),tol);

    Scalar fieldBase_result;
    stk::mesh::field_dot(fieldBase_result,*fixture.fieldBase2,*fixture.fieldBase3);
    EXPECT_NEAR(fieldBase_result,expected_result23*Scalar(fixture.numEntitiesMPI),tol);
}

template<class Scalar>
void test_coordinate_dot(BLASFixture3d<std::complex<Scalar> > &fixture,const double tol=1.5e-3)
{
    std::complex<Scalar> result=std::complex<Scalar>(0.0);
    for (int i=0;i<3;i++) result+=fixture.init1[i]*fixture.init2[i];
    std::complex<Scalar> result2=std::complex<Scalar>(0.0);
    for (int i=0;i<3;i++) result2+=fixture.init2[i]*fixture.init3[i];

    EXPECT_LT(std::abs(stk::mesh::field_dot(*fixture.field1,*fixture.field2)-result*Scalar(fixture.numEntitiesMPI)),tol);
    std::complex<Scalar> tmp;
    stk::mesh::field_dot(tmp,*fixture.fieldBase2,*fixture.fieldBase3);
    EXPECT_LT(std::abs(tmp-result2*Scalar(fixture.numEntitiesMPI)),tol);
}

TEST(FieldBLAS,coordinate_dot_double)
{
    double init1 [3]   = {4.21,1.23,-2.13};
    double init2 [3]   = {1.32,4.17,11.27};
    double init3 [3]   = {0.24,-7.11,3.21};
    BLASFixture3d<double> fixture (init1,init2,init3);
    test_coordinate_dot(fixture);
}

TEST(FieldBLAS,coordinate_dot_float)
{
    float init1 [3]   = {4.2,1.2,-2.1};
    float init2 [3]   = {1.3,4.1,11.2};
    float init3 [3]   = {0.2,-7.1,3.2};
    BLASFixture3d<float> fixture (init1,init2,init3);
    test_coordinate_dot(fixture,0.5);
}

TEST(FieldBLAS,coordinate_dot_complex)
{
    std::complex<double>   init1 [3] = {std::complex<double>(4.21,0.24),std::complex<double>(1.23,-0.1),std::complex<double>(-2.13,4.11)};
    std::complex<double>   init2 [3] = {std::complex<double>(1.32,23.1),std::complex<double>(4.17,-0.2),std::complex<double>(11.27,4.21)};
    std::complex<double>   init3 [3] = {std::complex<double>(0.24,-1.22),std::complex<double>(-7.11,42.1),std::complex<double>(3.21,7.11)};
    BLASFixture3d<std::complex<double> > fixture (init1,init2,init3);

    test_coordinate_dot(fixture);
}

TEST(FieldBLAS,coordinate_dot_int)
{
    int init1 [3] = {4,1,-2};
    int init2 [3] = {3,4,11};
    int init3 [3] = {8,-7,3};
    BLASFixture3d<int> fixture (init1,init2,init3);

    test_coordinate_dot(fixture);
}

template<class Scalar>
void test_coordinate_scale(BLASFixture3d<Scalar> &fixture,const Scalar alpha)
{
    Scalar result2 [3];
    for (int i=0;i<3;i++) result2[i]=fixture.init2[i]*alpha;
    Scalar result3 [3];
    for (int i=0;i<3;i++) result3[i]=fixture.init3[i]*alpha;

    stk::mesh::field_scale(alpha,*fixture.field2);
    stk::mesh::field_scale(alpha,*fixture.fieldBase3);

    EXPECT_TRUE(test3dfield(*fixture.field1,fixture.init1));
    EXPECT_TRUE(test3dfield(*fixture.field2,result2));
    EXPECT_TRUE(test3dfield(*fixture.field3,result3));
}


TEST(FieldBLAS,coordinate_scale_double)
{
    const double alpha = 4.11;
    double init1 [3]   = {4.21,1.23,-2.13};
    double init2 [3]   = {1.32,4.17,11.27};
    double init3 [3]   = {0.24,-7.11,3.21};
    BLASFixture3d<double> fixture (init1,init2,init3);

    test_coordinate_scale<double>(fixture,alpha);
}

TEST(FieldBLAS,coordinate_scale_float)
{
    const float alpha = 4.1;
    float init1 [3]   = {4.2,1.2,-2.1};
    float init2 [3]   = {1.3,4.1,11.2};
    float init3 [3]   = {0.2,-7.1,3.2};
    BLASFixture3d<float> fixture (init1,init2,init3);

    test_coordinate_scale<float>(fixture,alpha);
}

TEST(FieldBLAS,coordinate_scale_complex)
{
    const std::complex<double> alpha = std::complex<double>(4.11,-32.1);
    std::complex<double>   init1 [3] = {std::complex<double>(4.21,0.24),std::complex<double>(1.23,-0.1),std::complex<double>(-2.13,4.11)};
    std::complex<double>   init2 [3] = {std::complex<double>(1.32,23.1),std::complex<double>(4.17,-0.2),std::complex<double>(11.27,4.21)};
    std::complex<double>   init3 [3] = {std::complex<double>(0.24,-1.22),std::complex<double>(-7.11,42.1),std::complex<double>(3.21,7.11)};
    BLASFixture3d<std::complex<double> > fixture (init1,init2,init3);

    test_coordinate_scale<std::complex<double> >(fixture,alpha);
}

TEST(FieldBLAS,coordinate_scale_int)
{
    const int alpha = 4;
    int init1 [3] = {4,1,-2};
    int init2 [3] = {3,4,11};
    int init3 [3] = {8,-7,3};
    BLASFixture3d<int> fixture (init1,init2,init3);

    test_coordinate_scale<int>(fixture,alpha);
}

template<class Scalar>
void test_coordinate_fill(BLASFixture3d<Scalar> &fixture,const Scalar alpha)
{
    Scalar alpha_list [3] = {alpha,alpha,alpha};
    stk::mesh::field_fill(alpha,*fixture.field2);
    stk::mesh::field_fill(alpha,*fixture.fieldBase3);

    EXPECT_TRUE(test3dfield(*fixture.field1,fixture.init1));
    EXPECT_TRUE(test3dfield(*fixture.field2,alpha_list));
    EXPECT_TRUE(test3dfield(*fixture.field3,alpha_list));
}


TEST(FieldBLAS,coordinate_fill_double)
{
    const double alpha = 4.11;
    double init1 [3]   = {4.21,1.23,-2.13};
    double init2 [3]   = {1.32,4.17,11.27};
    double init3 [3]   = {0.24,-7.11,3.21};
    BLASFixture3d<double> fixture (init1,init2,init3);
    test_coordinate_fill<double>(fixture,alpha);
}

TEST(FieldBLAS,coordinate_fill_float)
{
    const float alpha = 4.1;
    float init1 [3]   = {4.2,1.2,-2.1};
    float init2 [3]   = {1.3,4.1,11.2};
    float init3 [3]   = {0.2,-7.1,3.2};
    BLASFixture3d<float> fixture (init1,init2,init3);
    test_coordinate_fill<float>(fixture,alpha);
}

TEST(FieldBLAS,coordinate_fill_complex)
{
    const std::complex<double> alpha = std::complex<double>(4.11,-32.1);
    std::complex<double>   init1 [3] = {std::complex<double>(4.21,0.24),std::complex<double>(1.23,-0.1),std::complex<double>(-2.13,4.11)};
    std::complex<double>   init2 [3] = {std::complex<double>(1.32,23.1),std::complex<double>(4.17,-0.2),std::complex<double>(11.27,4.21)};
    std::complex<double>   init3 [3] = {std::complex<double>(0.24,-1.22),std::complex<double>(-7.11,42.1),std::complex<double>(3.21,7.11)};
    BLASFixture3d<std::complex<double> > fixture (init1,init2,init3);

    test_coordinate_fill<std::complex<double> >(fixture,alpha);
}

TEST(FieldBLAS,coordinate_fill_int)
{
    const int alpha = 4;
    int init1 [3] = {4,1,-2};
    int init2 [3] = {3,4,11};
    int init3 [3] = {8,-7,3};
    BLASFixture3d<int> fixture (init1,init2,init3);

    test_coordinate_fill<int>(fixture,alpha);
}

template<class Scalar>
void test_coordinate_fill_component(BLASFixture3d<Scalar> &fixture,const Scalar* alpha)
{
    stk::mesh::field_fill_component(alpha,*fixture.field2);
    stk::mesh::field_fill_component(alpha,*fixture.fieldBase3);

    EXPECT_TRUE(test3dfield(*fixture.field1,fixture.init1));
    EXPECT_TRUE(test3dfield(*fixture.field2,alpha));
    EXPECT_TRUE(test3dfield(*fixture.field3,alpha));
}


TEST(FieldBLAS,coordinate_fill_component_double)
{
    double alpha [3] = {4.11,2.11,-3.12};
    double init1 [3] = {4.21,1.23,-2.13};
    double init2 [3] = {1.32,4.17,11.27};
    double init3 [3] = {0.24,-7.11,3.21};
    BLASFixture3d<double> fixture (init1,init2,init3);
    test_coordinate_fill_component<double>(fixture,alpha);
}

TEST(FieldBLAS,coordinate_fill_component_float)
{
    float alpha [3] = {4.1,2.1,-3.1};
    float init1 [3] = {4.2,1.2,-2.1};
    float init2 [3] = {1.3,4.1,11.2};
    float init3 [3] = {0.2,-7.1,3.2};
    BLASFixture3d<float> fixture (init1,init2,init3);
    test_coordinate_fill_component<float>(fixture,alpha);
}

TEST(FieldBLAS,coordinate_fill_component_complex)
{
    std::complex<double> alpha [3] = {std::complex<double>(4.11,-3.1),std::complex<double>(2.17,-0.25),std::complex<double>(7.14,-38.1)};
    std::complex<double> init1 [3] = {std::complex<double>(4.21,0.24),std::complex<double>(1.23,-0.13),std::complex<double>(-2.13,4.11)};
    std::complex<double> init2 [3] = {std::complex<double>(1.32,23.1),std::complex<double>(4.17,-0.24),std::complex<double>(11.27,4.21)};
    std::complex<double> init3 [3] = {std::complex<double>(0.24,-1.22),std::complex<double>(-7.11,42.1),std::complex<double>(3.21,7.11)};
    BLASFixture3d<std::complex<double> > fixture (init1,init2,init3);

    test_coordinate_fill_component<std::complex<double> >(fixture,alpha);
}

TEST(FieldBLAS,coordinate_fill_component_int)
{
    int alpha [3] = {2,-3,6};
    int init1 [3] = {4,1,-2};
    int init2 [3] = {3,4,11};
    int init3 [3] = {8,-7,3};
    BLASFixture3d<int> fixture (init1,init2,init3);

    test_coordinate_fill_component<int>(fixture,alpha);
}

template<class Scalar>
void test_coordinate_swap(BLASFixture3d<Scalar> &fixture)
{
    stk::mesh::field_swap(*fixture.field1,*fixture.field2);
    stk::mesh::field_swap(*fixture.fieldBase3,*fixture.fieldBase1);

    EXPECT_TRUE(test3dfield(*fixture.field1,fixture.init3));
    EXPECT_TRUE(test3dfield(*fixture.field2,fixture.init1));
    EXPECT_TRUE(test3dfield(*fixture.field3,fixture.init2));
}


TEST(FieldBLAS,coordinate_swap_double)
{
    double init1 [3]   = {4.21,1.23,-2.13};
    double init2 [3]   = {1.32,4.17,11.27};
    double init3 [3]   = {0.24,-7.11,3.21};
    BLASFixture3d<double> fixture (init1,init2,init3);
    test_coordinate_swap<double>(fixture);
}

TEST(FieldBLAS,coordinate_swap_float)
{
    float init1 [3]   = {4.2,1.2,-2.1};
    float init2 [3]   = {1.3,4.1,11.2};
    float init3 [3]   = {0.2,-7.1,3.2};
    BLASFixture3d<float> fixture (init1,init2,init3);
    test_coordinate_swap<float>(fixture);
}

TEST(FieldBLAS,coordinate_swap_complex)
{
    std::complex<double>   init1 [3] = {std::complex<double>(4.21,0.24),std::complex<double>(1.23,-0.1),std::complex<double>(-2.13,4.11)};
    std::complex<double>   init2 [3] = {std::complex<double>(1.32,23.1),std::complex<double>(4.17,-0.2),std::complex<double>(11.27,4.21)};
    std::complex<double>   init3 [3] = {std::complex<double>(0.24,-1.22),std::complex<double>(-7.11,42.1),std::complex<double>(3.21,7.11)};
    BLASFixture3d<std::complex<double> > fixture (init1,init2,init3);

    test_coordinate_swap<std::complex<double> >(fixture);
}

TEST(FieldBLAS,coordinate_swap_int)
{
    int init1 [3] = {4,1,-2};
    int init2 [3] = {3,4,11};
    int init3 [3] = {8,-7,3};
    BLASFixture3d<int> fixture (init1,init2,init3);

    test_coordinate_swap<int>(fixture);
}

template<class Scalar>
void test_coordinate_nrm2(BLASFixture3d<Scalar> &fixture,const double tol=1.5e-3)
{
    double result1=0.0;
    for (int i=0;i<3;i++) result1+=pow(std::abs(fixture.init1[i]),2.0);
    result1=sqrt(result1);
    double result2=0.0;
    for (int i=0;i<3;i++) result2+=pow(std::abs(fixture.init2[i]),2.0);
    result2=sqrt(result2);

    EXPECT_LT(std::abs(stk::mesh::field_nrm2(*fixture.field1)-Scalar(result1*sqrt(double(fixture.numEntitiesMPI)))),tol);
    Scalar tmp;
    stk::mesh::field_nrm2(tmp,*fixture.fieldBase2);
    EXPECT_LT(std::abs(tmp-Scalar(result2*sqrt(double(fixture.numEntitiesMPI)))),tol);
}


TEST(FieldBLAS,coordinate_nrm2_double)
{
    double init1 [3]   = {4.21,1.23,-2.13};
    double init2 [3]   = {1.32,4.17,11.27};
    double init3 [3]   = {0.24,-7.11,3.21};
    BLASFixture3d<double> fixture (init1,init2,init3);
    test_coordinate_nrm2<double>(fixture);
}

TEST(FieldBLAS,coordinate_nrm2_float)
{
    float init1 [3]   = {4.2,1.2,-2.1};
    float init2 [3]   = {1.3,4.1,11.2};
    float init3 [3]   = {0.2,-7.1,3.2};
    BLASFixture3d<float> fixture (init1,init2,init3);
    test_coordinate_nrm2<float>(fixture,0.5);
}

TEST(FieldBLAS,coordinate_nrm2_complex)
{
    std::complex<double>   init1 [3] = {std::complex<double>(4.21,0.24),std::complex<double>(1.23,-0.1),std::complex<double>(-2.13,4.11)};
    std::complex<double>   init2 [3] = {std::complex<double>(1.32,23.1),std::complex<double>(4.17,-0.2),std::complex<double>(11.27,4.21)};
    std::complex<double>   init3 [3] = {std::complex<double>(0.24,-1.22),std::complex<double>(-7.11,42.1),std::complex<double>(3.21,7.11)};
    BLASFixture3d<std::complex<double> > fixture (init1,init2,init3);

    test_coordinate_nrm2<std::complex<double> >(fixture);
}

TEST(FieldBLAS,coordinate_nrm2_int)
{
    int init1 [3] = {4,1,-2};
    int init2 [3] = {3,4,11};
    int init3 [3] = {8,-7,3};
    BLASFixture3d<int> fixture (init1,init2,init3);

    test_coordinate_nrm2<int>(fixture,2);
}

template<class Scalar>
void test_coordinate_asum(BLASFixture3d<Scalar> &fixture,const double tol=1.5e-3)
{
    Scalar result1=Scalar(0.0);
    for (int i=0;i<3;i++) result1+=std::abs(fixture.init1[i]);
    Scalar result2=Scalar(0.0);
    for (int i=0;i<3;i++) result2+=std::abs(fixture.init2[i]);

    EXPECT_LT(std::abs(stk::mesh::field_asum(*fixture.field1)-result1*Scalar(fixture.numEntitiesMPI)),tol);
    Scalar tmp;
    stk::mesh::field_asum(tmp,*fixture.fieldBase2);
    EXPECT_LT(std::abs(tmp-result2*Scalar(fixture.numEntitiesMPI)),tol);
}


TEST(FieldBLAS,coordinate_asum_double)
{
    double init1 [3]   = {4.21,1.23,-2.13};
    double init2 [3]   = {1.32,4.17,11.27};
    double init3 [3]   = {0.24,-7.11,3.21};
    BLASFixture3d<double> fixture (init1,init2,init3);
    test_coordinate_asum<double>(fixture);
}

TEST(FieldBLAS,coordinate_asum_float)
{
    float init1 [3]   = {4.2,1.2,-2.1};
    float init2 [3]   = {1.3,4.1,11.2};
    float init3 [3]   = {0.2,-7.1,3.2};
    BLASFixture3d<float> fixture (init1,init2,init3);
    test_coordinate_asum<float>(fixture,0.5);
}

TEST(FieldBLAS,coordinate_asum_complex)
{
    std::complex<double>   init1 [3] = {std::complex<double>(4.21,0.24),std::complex<double>(1.23,-0.1),std::complex<double>(-2.13,4.11)};
    std::complex<double>   init2 [3] = {std::complex<double>(1.32,23.1),std::complex<double>(4.17,-0.2),std::complex<double>(11.27,4.21)};
    std::complex<double>   init3 [3] = {std::complex<double>(0.24,-1.22),std::complex<double>(-7.11,42.1),std::complex<double>(3.21,7.11)};
    BLASFixture3d<std::complex<double> > fixture (init1,init2,init3);

    test_coordinate_asum<std::complex<double> >(fixture);
}

TEST(FieldBLAS,coordinate_asum_int)
{
    int init1 [3] = {4,1,-2};
    int init2 [3] = {3,4,11};
    int init3 [3] = {8,-7,3};
    BLASFixture3d<int> fixture (init1,init2,init3);

    test_coordinate_asum<int>(fixture);
}
