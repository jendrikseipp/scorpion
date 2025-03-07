from .plan_manager import PlanManager
from .run_components import PREPROCESSED_OUTPUT

def cleanup_temporary_files(args):
    args.sas_file.unlink(missing_ok=True)
    PREPROCESSED_OUTPUT.unlink(missing_ok=True)
    PlanManager(args.plan_file).delete_existing_plans()
