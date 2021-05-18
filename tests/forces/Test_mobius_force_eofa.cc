/*************************************************************************************

Grid physics library, www.github.com/paboyle/Grid

Source file: ./tests/forces/Test_dwf_force_eofa.cc

Copyright (C) 2017

Author: Peter Boyle <paboyle@ph.ed.ac.uk>
Author: David Murphy <dmurphy@phys.columbia.edu>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

See the full license in the file "LICENSE" in the top level distribution directory
*************************************************************************************/
/*  END LEGAL */

#include <Grid/Grid.h>

using namespace std;
using namespace Grid;
 ;

int main (int argc, char** argv)
{
  Grid_init(&argc, &argv);

  Coordinate latt_size   = GridDefaultLatt();
  Coordinate simd_layout = GridDefaultSimd(Nd,vComplex::Nsimd());
  Coordinate mpi_layout  = GridDefaultMpi();

  const int Ls = 8;

  GridCartesian         *UGrid   = SpaceTimeGrid::makeFourDimGrid(GridDefaultLatt(), GridDefaultSimd(Nd,vComplex::Nsimd()), GridDefaultMpi());
  GridRedBlackCartesian *UrbGrid = SpaceTimeGrid::makeFourDimRedBlackGrid(UGrid);
  GridCartesian         *FGrid   = SpaceTimeGrid::makeFiveDimGrid(Ls, UGrid);
  GridRedBlackCartesian *FrbGrid = SpaceTimeGrid::makeFiveDimRedBlackGrid(Ls, UGrid);

  // Want a different conf at every run
  // First create an instance of an engine.
  std::random_device rnd_device;
  // Specify the engine and distribution.
  std::mt19937 mersenne_engine(rnd_device());
  std::uniform_int_distribution<int> dist(1, 100);

  auto gen = std::bind(dist, mersenne_engine);
  std::vector<int> seeds4(4);
  generate(begin(seeds4), end(seeds4), gen);

  //std::vector<int> seeds4({1,2,3,5});
  std::vector<int> seeds5({5,6,7,8});
  GridParallelRNG RNG5(FGrid);  RNG5.SeedFixedIntegers(seeds5);
  GridParallelRNG RNG4(UGrid);  RNG4.SeedFixedIntegers(seeds4);

  int threads = GridThread::GetThreads();
  std::cout << GridLogMessage << "Grid is setup to use " << threads << " threads" << std::endl;

  LatticeFermion phi        (FGrid);  gaussian(RNG5, phi);
  LatticeFermion Mphi       (FGrid);
  LatticeFermion MphiPrime  (FGrid);

  LatticeGaugeField U(UGrid);
  SU<Nc>::HotConfiguration(RNG4,U);

  ////////////////////////////////////
  // Unmodified matrix element
  ////////////////////////////////////
  RealD b  = 2.5;
  RealD c  = 1.5;
  RealD mf = 0.01;
  RealD mb = 1.0;
  RealD M5 = 1.8;
  MobiusEOFAFermionR Lop(U, *FGrid, *FrbGrid, *UGrid, *UrbGrid, mf, mf, mb, 0.0, -1, M5, b, c);
  MobiusEOFAFermionR Rop(U, *FGrid, *FrbGrid, *UGrid, *UrbGrid, mb, mf, mb, -1.0, 1, M5, b, c);
  OneFlavourRationalParams Params(0.95, 100.0, 5000, 1.0e-12, 12);
  ConjugateGradient<LatticeFermion> CG(1.0e-12, 5000);
  ExactOneFlavourRatioPseudoFermionAction<WilsonImplR> Meofa(Lop, Rop, CG, CG, CG, CG, CG, Params, false);

  GridSerialRNG  sRNG; sRNG.SeedFixedIntegers(seeds4);

  //Check the rational approximation
  {    
    RealD scale = std::sqrt(0.5);
    LatticeFermion eta    (Lop.FermionGrid());
    gaussian(RNG5,eta); eta = eta * scale;

    Meofa.refresh(U, eta);
    
    //Phi = M^{-1/2} eta
    //M is Hermitian    
    //(Phi, M Phi) = eta^\dagger  M^{-1/2} M M^{-1/2} eta = eta^\dagger eta
    LatticeFermion phi = Meofa.getPhi();
    LatticeFermion Mphi(FGrid);
    
    Meofa.Meofa(U, phi, Mphi);
    std::cout << "Computing inner product" << std::endl;
    ComplexD inner = innerProduct(phi, Mphi);
    ComplexD test = inner - norm2(eta);
    
    std::cout << "(phi, Mphi) - (eta,eta): " << test << "  expect 0" << std::endl;

    assert(test.real() < 1e-8);
    assert(test.imag() < 1e-8);

    //Another test is to use heatbath twice to apply M^{-1/2} to Phi then apply M
    // M  Phi' 
    //= M M^{-1/2} Phi 
    //= M M^{-1/2} M^{-1/2} eta 
    //= eta
    Meofa.refresh(U, phi);
    LatticeFermion phi2 = Meofa.getPhi();
    LatticeFermion test2(FGrid);
    Meofa.Meofa(U, phi2, test2);
    test2  = test2 - eta;
    RealD test2_norm = norm2(test2);
    std::cout << "|M M^{-1/2} M^{-1/2} eta - eta|^2 = " << test2_norm << " expect 0" << std::endl;
    assert( test2_norm < 1e-8 );
  }


  Meofa.refresh(U, sRNG, RNG5 );


    













  RealD S = Meofa.S(U); // pdag M p

  // get the deriv of phidag M phi with respect to "U"
  LatticeGaugeField UdSdU(UGrid);
  Meofa.deriv(U, UdSdU);

  ////////////////////////////////////
  // Modify the gauge field a little
  ////////////////////////////////////
  RealD dt = 0.0001;

  LatticeColourMatrix mommu(UGrid);
  LatticeColourMatrix forcemu(UGrid);
  LatticeGaugeField mom(UGrid);
  LatticeGaugeField Uprime(UGrid);

  for(int mu=0; mu<Nd; mu++){

    SU<Nc>::GaussianFundamentalLieAlgebraMatrix(RNG4, mommu); // Traceless antihermitian momentum; gaussian in lie alg

    PokeIndex<LorentzIndex>(mom, mommu, mu);

    // fourth order exponential approx
    autoView( mom_v, mom, CpuRead);
    autoView( U_v , U, CpuRead);
    autoView(Uprime_v, Uprime, CpuWrite);

    thread_foreach(i,mom_v,{
      Uprime_v[i](mu) =	  U_v[i](mu)
	+ mom_v[i](mu)*U_v[i](mu)*dt 
	+ mom_v[i](mu) *mom_v[i](mu) *U_v[i](mu)*(dt*dt/2.0)
	+ mom_v[i](mu) *mom_v[i](mu) *mom_v[i](mu) *U_v[i](mu)*(dt*dt*dt/6.0)
	+ mom_v[i](mu) *mom_v[i](mu) *mom_v[i](mu) *mom_v[i](mu) *U_v[i](mu)*(dt*dt*dt*dt/24.0)
	+ mom_v[i](mu) *mom_v[i](mu) *mom_v[i](mu) *mom_v[i](mu) *mom_v[i](mu) *U_v[i](mu)*(dt*dt*dt*dt*dt/120.0)
	+ mom_v[i](mu) *mom_v[i](mu) *mom_v[i](mu) *mom_v[i](mu) *mom_v[i](mu) *mom_v[i](mu) *U_v[i](mu)*(dt*dt*dt*dt*dt*dt/720.0)
	;
    });
  }

  /*Ddwf.ImportGauge(Uprime);
  Ddwf.M          (phi,MphiPrime);

  ComplexD Sprime    = innerProduct(MphiPrime   ,MphiPrime);*/
  RealD Sprime = Meofa.S(Uprime);

  //////////////////////////////////////////////
  // Use derivative to estimate dS
  //////////////////////////////////////////////

  LatticeComplex dS(UGrid);
  dS = Zero();
  for(int mu=0; mu<Nd; mu++){
    mommu = PeekIndex<LorentzIndex>(UdSdU, mu);
    mommu = Ta(mommu)*2.0;
    PokeIndex<LorentzIndex>(UdSdU, mommu, mu);
  }

  for(int mu=0; mu<Nd; mu++){
    forcemu = PeekIndex<LorentzIndex>(UdSdU, mu);
    mommu   = PeekIndex<LorentzIndex>(mom, mu);

    // Update PF action density
    dS = dS + trace(mommu*forcemu)*dt;
  }

  ComplexD dSpred = sum(dS);

  /*std::cout << GridLogMessage << " S      " << S << std::endl;
  std::cout << GridLogMessage << " Sprime " << Sprime << std::endl;
  std::cout << GridLogMessage << "dS      " << Sprime-S << std::endl;
  std::cout << GridLogMessage << "predict dS    " << dSpred << std::endl;*/
  printf("\nS = %1.15e\n", S);
  printf("Sprime = %1.15e\n", Sprime);
  printf("dS = %1.15e\n", Sprime - S);
  printf("real(dS_predict) = %1.15e\n", dSpred.real());
  printf("imag(dS_predict) = %1.15e\n\n", dSpred.imag());

  assert( fabs(real(Sprime-S-dSpred)) < 1.0 ) ;

  std::cout << GridLogMessage << "Done" << std::endl;
  Grid_finalize();
}
