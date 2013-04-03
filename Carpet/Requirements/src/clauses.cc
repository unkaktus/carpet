
#include <iostream>
#include <vector>

#include <cctk.h>
#include <cctki_Schedule.h>

#include <clause.hh>
#include <clauses.hh>
#include <util.hh>

using namespace std;

namespace Requirements {

  void clauses_t::setup(cFunctionData const* const function_data)
  {
    clause_t prototype;
    prototype.interpret_options(function_data);
    reads.reserve(function_data->n_ReadsClauses);
    for (int n=0; n<function_data->n_ReadsClauses; ++n) {
      clause_t clause(prototype);
      clause.parse_clause(function_data->ReadsClauses[n]);
      reads.push_back(clause);
    }
    writes.reserve(function_data->n_WritesClauses);
    for (int n=0; n<function_data->n_WritesClauses; ++n) {
      clause_t clause(prototype);
      clause.parse_clause(function_data->WritesClauses[n]);
      writes.push_back(clause);
    }
  }

  void clauses_t::output(ostream& os) const
  {
    os << "reads = " << reads << ", writes = " << writes;
  }

}