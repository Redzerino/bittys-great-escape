#include "vm.h"
#include <assert.h>
#include <stdio.h>

static void Check(const char *source,const char *function,const int *args,int argc,
                  int expected,bool needBranch,int minimumDepth)
{
    VmBuildResult build;
    VmOptions options={false,200000};
    Vm *vm=Vm_Build(source,&options,NULL,&build);
    assert(vm&&build.ok);
    assert(Vm_Start(vm,function,args,argc));
    bool sawBranch=false,sawOperation=false;
    int depth=0;
    for(int steps=0;steps<1000;steps++)
    {
        Vm_PauseAtNextLine(vm);
        VmStatus status=Vm_Run(vm,1000);
        VmVisualTrace trace;
        Vm_GetVisualTrace(vm,&trace);
        sawBranch|=trace.branchKnown;
        sawOperation|=trace.operationKnown;
        if(trace.depth>depth)depth=trace.depth;
        assert(status!=VM_ERROR);
        if(status==VM_FINISHED)
        {
            assert(Vm_ReturnValue(vm)==expected);
            assert(trace.returnKnown&&trace.returnValue==expected);
            assert(!needBranch||sawBranch);
            assert(sawOperation);
            assert(depth>=minimumDepth);
            Vm_Destroy(vm);
            return;
        }
    }
    assert(!"program did not finish");
}
int main(void)
{
    int a[]={4};
    Check("int choose(int a) {\nif(a>2) return a*3;\nreturn 0;\n}","choose",a,1,12,true,1);
    int b[]={1};
    Check("int choose(int a) {\nif(a>2) return a*3;\nreturn 0;\n}","choose",b,1,0,true,1);
    int power[]={2,3};
    Check("int power(int b,int n) {\nint result=1;\nfor(int i=0;i<n;i++){\nresult=result*b;\n}\nreturn result;\n}","power",power,2,8,true,1);
    int recursion[]={5};
    Check("int factorial(int n) {\nif(n<=1) return 1;\nreturn n*factorial(n-1);\n}","factorial",recursion,1,120,true,4);
    puts("PASS: true/false branches, arithmetic, loops, recursion and return telemetry");
    return 0;
}
