class SM {
    public:
        // Constructor
        SM ();

        bool Fetch();
        bool Decode();
        bool Execute();
        bool Mem();
        bool WriteBack();
};