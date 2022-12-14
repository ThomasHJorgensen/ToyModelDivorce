#ifndef MAIN
#define SINGLE
#include "myheader.cpp"
#endif

namespace single {

    double cons_from_Ctot(double Ctot,double hours,int iH,int gender,sol_struct* sol,par_struct* par){
        // interpolate in pre-computed object
        double* pre = sol->pre_cons_w_single;
        if (gender==man){
            pre = sol->pre_cons_m_single;
        }

        int idx = index::index3(iH,0,0,par->num_H,par->num_pre_h,par->num_pre_C);
        return tools::interp_2d(par->grid_pre_hours,par->grid_pre_Ctot,par->num_pre_h,par->num_pre_C,&pre[idx],hours,Ctot);
        
    }

    typedef struct {
        double resources;
        double hours;
        double leisure;

        double* V_next;
        double A;
        double K;
        double H;
        int iH;
        int gender;
        sol_struct *sol;
        par_struct *par;

        double* cons;
        double* market;
        double cons_init;
        double market_init;

        double Ctot;

    } solver_struct;
    
    double objfunc_single_last(unsigned n, const double *x, double *grad, void *solver_data_in){
        // unpack
        solver_struct *solver_data = (solver_struct *) solver_data_in;

        double hours = x[0];

        double Ctot = solver_data->Ctot;
        double H = solver_data->H;
        int iH = solver_data->iH;
        int gender = solver_data->gender;
        sol_struct *sol = solver_data->sol;
        par_struct *par = solver_data->par;

        // implied variables                    
        double cons = cons_from_Ctot(Ctot,hours,iH,gender,sol,par);
        double market = Ctot - cons;
        double leisure = par->max_time - hours;

        // home production
        double home_prod = utils::home_prod_single(hours,market,H,gender,par);

        return - utils::util(cons,leisure,home_prod,gender,par);

    }
    
    // Solve for total consumption, Ctot, given other choices
    double value_of_Ctot(double Ctot,double hours, double leisure,double resources, int iH, double H, double K, double A,int gender, double* V_next,sol_struct *sol, par_struct *par){
        // implied variables                    
        double cons = cons_from_Ctot(Ctot,hours,iH,gender,sol,par);
        double market = Ctot - cons;
        double labor = utils::labor_implied(leisure,hours,par);

        // home production 
        double home_prod = utils::home_prod_single(hours,market,H,gender,par);
        double Util = utils::util(cons,leisure,home_prod,gender,par);

        // continuation value
        double A_next = resources - Ctot;
        double K_next = utils::trans_K(K,labor,par);
        double H_next = utils::trans_H(H,hours,par);

        double* grid_A = par->grid_Aw;
        double* grid_H = par->grid_Hw;
        double* grid_K = par->grid_K;
        if (gender==man){
            grid_A = par->grid_Am;
            grid_H = par->grid_Hm;
        }

        double V_next_interp = tools::interp_3d(grid_H,grid_K,grid_A,par->num_H,par->num_K,par->num_A,V_next,H_next,K_next,A_next);

        return Util + par->beta*V_next_interp;
    }

    double objfunc_Ctot(unsigned n, const double *x, double *grad, void *solver_data_in){
        // unpack
        solver_struct *solver_data = (solver_struct *) solver_data_in;

        double Ctot = x[0];

        double* V_next = solver_data->V_next;
        double resources = solver_data->resources;
        double hours = solver_data->hours;
        double leisure = solver_data->leisure;
        double A = solver_data->A;
        double K = solver_data->K;
        double H = solver_data->H;
        int iH = solver_data->iH;
        int gender = solver_data->gender;
        sol_struct* sol = solver_data->sol;
        par_struct* par = solver_data->par;

        double val = value_of_Ctot(Ctot,hours,leisure,resources,iH,H,K,A,gender,V_next,sol,par);

        return -val;

    }
    
    double solve_Ctot(solver_struct * solver_data){
        
        // Add resources available for consumption to solver data
        double labor = utils::labor_implied(solver_data->leisure,solver_data->hours,solver_data->par);
        // double income = labor*utils::wage_func(solver_data->K,solver_data->par);
        // double resources = solver_data->par->R*solver_data->A + income;

        solver_data->resources = utils::resources_single(labor,solver_data->H,solver_data->K,solver_data->A,solver_data->par); //resources;
        
        // setup numerical solver
        const int dim = 1;
        double lb[dim],ub[dim],x[dim];
        
        auto opt = nlopt_create(NLOPT_LN_BOBYQA, dim); // NLOPT_LN_BOBYQA NLOPT_LD_MMA NLOPT_LD_LBFGS NLOPT_GN_ORIG_DIRECT
        nlopt_set_ftol_abs(opt,1.e-10);
        double minf=0.0;

        nlopt_set_min_objective(opt, objfunc_Ctot, solver_data);
            
        // bounds on share of total spending on consumption
        lb[0] = 1.0e-6;
        ub[0] = solver_data->resources;
        nlopt_set_lower_bounds(opt, lb);
        nlopt_set_upper_bounds(opt, ub);

        // optimize
        double init = solver_data->cons_init + solver_data->market_init;
        if (init>ub[0]){
            init = 0.5*ub[0];
        } else if(init<lb[0]){
            init = 0.5*ub[0];
        }
        // x[0] = init; //0.5 * ub[0];
        x[0] = 0.5 * ub[0];
        nlopt_optimize(opt, x, &minf);
        nlopt_destroy(opt);

        // store results in solver data
        double Ctot = x[0];
        solver_data->cons[0] = cons_from_Ctot(Ctot,solver_data->hours,solver_data->iH,solver_data->gender,solver_data->sol,solver_data->par);
        solver_data->market[0] = Ctot - solver_data->cons[0];

        return minf;
        
    }

    // solve the entire period problem. Optimize wrt. labor market work and home production.
    double objfunc_single(unsigned n, const double *x, double *grad, void *solver_data_in){
        // unpack
        double leisure = x[0];
        double hours = x[1];

        // add info to solver data
        solver_struct *solver_data = (solver_struct *) solver_data_in;
        solver_data->hours = hours; 
        solver_data->leisure = leisure; 

        // Time-constraint enforced through penalty. This makes the constraint behave better.
        double penalty = 0.0;
        double labor = solver_data->par->max_time - leisure - hours;
        if (labor<0.0){
            penalty = -labor * 10000.0;
        }

        return penalty + solve_Ctot(solver_data);

    }

    double time_constraint(unsigned n, const double *x, double *grad, void *solver_data_in){
        solver_struct *solver_data = (solver_struct *) solver_data_in;
        
        double leisure = x[0];
        double hours = x[1];

        par_struct* par = solver_data->par;

        return -(par->max_time - leisure - hours);
    }

    double solve_period_single(double* cons, double* market,double* leisure,double* hours,double A,double K,double H,int iH,int gender,double* V_next,sol_struct *sol,par_struct *par, double cons_init,double market_init,double leisure_init,double hours_init){
        
        // setup numerical solver        
        const int dim = 2;
        double lb[dim],ub[dim],x[dim];
        
        auto opt = nlopt_create(NLOPT_LN_COBYLA, dim); // with constraint this works well. but slow.
        // auto opt = nlopt_create(NLOPT_LN_BOBYQA, dim); // NLOPT_LN_BOBYQA NLOPT_LD_MMA NLOPT_LD_LBFGS NLOPT_GN_ORIG_DIRECT
        nlopt_set_ftol_abs(opt,1.e-8);
        double minf=0.0;

        solver_struct* solver_data = new solver_struct;
        solver_data->V_next = V_next;
        solver_data->A = A;
        solver_data->K = K;
        solver_data->H = H;
        solver_data->iH = iH;
        solver_data->gender = gender;
        solver_data->sol = sol;
        solver_data->par = par;

        solver_data->cons = cons;
        solver_data->market = market;
        solver_data->cons_init = cons_init;
        solver_data->market_init = market_init;
        
        nlopt_set_min_objective(opt, objfunc_single, solver_data);
            
        // bounds on hours worked and home production
        lb[0] = 1.0e-6;
        ub[0] = par->max_time;
        lb[1] = 1.0e-6;
        ub[1] = par->max_time;
        nlopt_set_lower_bounds(opt, lb);
        nlopt_set_upper_bounds(opt, ub);

        nlopt_add_inequality_constraint(opt, time_constraint, solver_data, 1e-8);

        // optimize
        x[0] = leisure_init;
        x[1] = hours_init;
        nlopt_optimize(opt, x, &minf);
        nlopt_destroy(opt);

        // store results. expenditures are stored automatically through solver_data.
        leisure[0] = x[0];
        hours[0] = x[1];

        return minf;
    }

    void solve_single(int t,sol_struct *sol,par_struct *par){
        
        // terminal period
        if (t == (par->T-1)){
            
            #pragma omp parallel num_threads(par->threads)
            {

                // 1. allocate objects for solver
                solver_struct* solver_data = new solver_struct;
                
                const int dim = 1;
                double lb[dim],ub[dim],x[dim];
                
                auto opt = nlopt_create(NLOPT_LN_BOBYQA, dim); //NLOPT_LN_BOBYQA NLOPT_LD_MMA NLOPT_LD_LBFGS NLOPT_GN_ORIG_DIRECT
                nlopt_set_ftol_abs(opt,1.e-8);
                double minf=0.0;

                // 2. loop over states
                #pragma omp for
                for (int iH=0; iH<par->num_H;iH++){
                    int iK = 0; // human capital does not matter here. So re-use this value below
                        for (int iA=0; iA<par->num_A;iA++){
                            int idx = index::index4(t,iH,iK,iA,par->T,par->num_H,par->num_K,par->num_A);

                            // state variables
                            double Aw = par->grid_Aw[iA];
                            double Am = par->grid_Am[iA];

                            double Hw = par->grid_Hw[iH];
                            double Hm = par->grid_Hm[iH];

                            // solve for women and men
                            // WOMEN
                            // settings
                            solver_data->Ctot = par->R*Aw;
                            solver_data->H = Hw;
                            solver_data->iH = iH;
                            solver_data->gender = woman;
                            solver_data->sol = sol;
                            solver_data->par = par;
                            nlopt_set_min_objective(opt, objfunc_single_last, solver_data);
                                
                            // bounds on home production hours
                            lb[0] = 1.0e-8;
                            ub[0] = par->max_time;

                            nlopt_set_lower_bounds(opt, lb);
                            nlopt_set_upper_bounds(opt, ub);

                            // optimize
                            if (iA==0){
                                x[0] = ub[0]/1.1;
                            }
                            
                            nlopt_optimize(opt, x, &minf);

                            // store results
                            double hours = x[0];
                            sol->hours_w_single[idx] = hours;
                            double c = cons_from_Ctot(solver_data->Ctot,hours,iH,woman,sol,par);
                            sol->market_w_single[idx] = solver_data->Ctot - c;
                            sol->cons_w_single[idx] = c;
                            sol->leisure_w_single[idx] = par->max_time - sol->hours_w_single[idx];
                            
                            sol->Vw_single[idx] = -minf;

                            // insert for all values of human capital
                            for (int iK_now=1; iK_now<par->num_K;iK_now++){
                                int idx_now = index::index4(t,iH,iK_now,iA,par->T,par->num_H,par->num_K,par->num_A);
                                sol->cons_w_single[idx_now] = sol->cons_w_single[idx];
                                sol->leisure_w_single[idx_now] = sol->leisure_w_single[idx];
                                sol->hours_w_single[idx_now] = sol->hours_w_single[idx];
                                sol->market_w_single[idx_now] = sol->market_w_single[idx];
                                sol->Vw_single[idx_now] = sol->Vw_single[idx];
                            }


                            // MEN
                            // settings
                            solver_data->Ctot = par->R*Am;
                            solver_data->H = Hm;
                            solver_data->iH = iH;
                            solver_data->gender = man;
                            solver_data->sol = sol;
                            solver_data->par = par;
                            nlopt_set_min_objective(opt, objfunc_single_last, solver_data);
                                
                            // bounds on home production hours
                            lb[0] = 1.0e-8;
                            ub[0] = par->max_time;

                            nlopt_set_lower_bounds(opt, lb);
                            nlopt_set_upper_bounds(opt, ub);

                            // optimize
                            if (iA==0){
                                x[0] = ub[0]/1.1;
                            }
                            
                            nlopt_optimize(opt, x, &minf);

                            // store results
                            hours = x[0];
                            sol->hours_m_single[idx] = hours;
                            c = cons_from_Ctot(solver_data->Ctot,hours,iH,man,sol,par);
                            sol->market_m_single[idx] = solver_data->Ctot - c;
                            sol->cons_m_single[idx] = c;
                            sol->leisure_m_single[idx] = par->max_time - sol->hours_m_single[idx];
                            
                            sol->Vm_single[idx] = -minf;

                            // insert for all values of human capital
                            for (int iK_now=1; iK_now<par->num_K;iK_now++){
                                int idx_now = index::index4(t,iH,iK_now,iA,par->T,par->num_H,par->num_K,par->num_A);
                                sol->cons_m_single[idx_now] = sol->cons_m_single[idx];
                                sol->leisure_m_single[idx_now] = sol->leisure_m_single[idx];
                                sol->hours_m_single[idx_now] = sol->hours_m_single[idx];
                                sol->market_m_single[idx_now] = sol->market_m_single[idx];
                                sol->Vm_single[idx_now] = sol->Vm_single[idx];
                            }

                        }
                
                    
                }

                nlopt_destroy(opt);

            } // pragma
        } else {
            
            #pragma omp parallel num_threads(par->threads)
            {
                double cons_init, market_init, leisure_init, hours_init;
                // 2. loop over states
                #pragma omp for
                for (int iH=0; iH<par->num_H;iH++){
                    for (int iK=0; iK<par->num_K;iK++){
                        for (int iA=0; iA<par->num_A;iA++){
                            
                            int idx = index::index4(t,iH,iK,iA,par->T,par->num_H,par->num_K,par->num_A);
                            int idx_next = index::index4(t+1,0,0,0,par->T,par->num_H,par->num_K,par->num_A);
                            int idx_last = index::index4(t,iH,iK,iA-1,par->T,par->num_H,par->num_K,par->num_A);

                            // state variables
                            double Aw = par->grid_Aw[iA];
                            double Am = par->grid_Am[iA];

                            double Hw = par->grid_Hw[iH];
                            double Hm = par->grid_Hm[iH];

                            double K = par->grid_K[iK];

                            // initial values
                            cons_init = 0.001;
                            market_init = 0.001;
                            leisure_init = par->max_time/3.0;
                            hours_init = par->max_time/3.0;
                            if (iA>0){
                                cons_init = sol->cons_w_single[idx_last];
                                market_init = sol->market_w_single[idx_last];
                                leisure_init = sol->leisure_w_single[idx_last];
                                hours_init = sol->hours_w_single[idx_last];
                            }
                            
                            // solve model for women
                            double* cons = &sol->cons_w_single[idx];
                            double* market = &sol->market_w_single[idx];
                            double* leisure = &sol->leisure_w_single[idx];
                            double* hours = &sol->hours_w_single[idx];
                            double* V_next = &sol->Vw_single[idx_next];
                            
                            double obj = solve_period_single(cons, market, leisure, hours, 
                                                        Aw, K, Hw,iH,woman,V_next,sol,par,
                                                        cons_init, market_init, leisure_init, hours_init);
                            
                            sol->Vw_single[idx] = -obj;

                            // solve model for men
                            if (iA>0){
                                cons_init = sol->cons_m_single[idx_last];
                                market_init = sol->market_m_single[idx_last];
                                leisure_init = sol->leisure_m_single[idx_last];
                                hours_init = sol->hours_m_single[idx_last];
                            }

                            cons = &sol->cons_m_single[idx];
                            market = &sol->market_m_single[idx];
                            leisure = &sol->leisure_m_single[idx];
                            hours = &sol->hours_m_single[idx];
                            V_next = &sol->Vm_single[idx_next];
                            obj = solve_period_single(cons, market, leisure, hours,
                                                        Am, K, Hm,iH,man,V_next,sol,par,
                                                        cons_init, market_init, leisure_init, hours_init  );
                            
                            sol->Vm_single[idx] = -obj;

                        }
                    }
                }
            } // pragma
            
        }   
        
    }


}